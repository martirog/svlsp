#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Phase 5.3 — SymbolDatabase typed query API
// ---------------------------------------------------------------------------

namespace {
struct Fixture {
    Database      db{":memory:"};
    SymbolDatabase sdb{db};
    Fixture() { db.initSchema(); }
};
}

// ---------------------------------------------------------------------------
// File upsert / hash
// ---------------------------------------------------------------------------

TEST_CASE("upsertFile creates a new entry and returns positive id", "[db][symbol-db]") {
    Fixture f;
    auto id = f.sdb.upsertFile("/path/to/a.sv", "hash1");
    CHECK(id > 0);
}

TEST_CASE("upsertFile returns the same id for the same path", "[db][symbol-db]") {
    Fixture f;
    auto id1 = f.sdb.upsertFile("/a.sv", "hash1");
    auto id2 = f.sdb.upsertFile("/a.sv", "hash2");
    CHECK(id1 == id2);
}

TEST_CASE("getFileHash returns empty string for unknown path", "[db][symbol-db]") {
    Fixture f;
    CHECK(f.sdb.getFileHash("/nonexistent.sv").empty());
}

TEST_CASE("getFileHash returns the stored hash after upsert", "[db][symbol-db]") {
    Fixture f;
    f.sdb.upsertFile("/a.sv", "abc123");
    CHECK(f.sdb.getFileHash("/a.sv") == "abc123");
}

TEST_CASE("upsertFile updates the hash on second call", "[db][symbol-db]") {
    Fixture f;
    f.sdb.upsertFile("/a.sv", "old");
    f.sdb.upsertFile("/a.sv", "new");
    CHECK(f.sdb.getFileHash("/a.sv") == "new");
}

// ---------------------------------------------------------------------------
// Symbol storage and retrieval
// ---------------------------------------------------------------------------

TEST_CASE("replaceSymbols stores records and symbolsForFile returns them", "[db][symbol-db]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");

    std::vector<ParseRecord> recs{{ParseRecordKind::Module, "top", 1, 0, "", ""}};
    f.sdb.replaceSymbols(fid, recs);

    auto got = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(got.size() == 1);
    CHECK(got[0].name == "top");
    CHECK(got[0].kind == "Module");
    CHECK(got[0].line == 1);
}

TEST_CASE("replaceSymbols replaces existing symbols", "[db][symbol-db]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");

    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Module, "old", 1, 0, "", ""}});
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Module, "new", 2, 0, "", ""}});

    auto got = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(got.size() == 1);
    CHECK(got[0].name == "new");
}

TEST_CASE("symbolsForFile returns empty for unknown path", "[db][symbol-db]") {
    Fixture f;
    CHECK(f.sdb.symbolsForFile("/none.sv").empty());
}

TEST_CASE("findSymbolsByName finds across files", "[db][symbol-db]") {
    Fixture f;
    auto f1 = f.sdb.upsertFile("/a.sv", "h1");
    auto f2 = f.sdb.upsertFile("/b.sv", "h2");
    f.sdb.replaceSymbols(f1, {{ParseRecordKind::Module, "clk_gen", 1, 0, "", ""}});
    f.sdb.replaceSymbols(f2, {{ParseRecordKind::Module, "clk_gen", 5, 0, "", ""}});

    auto got = f.sdb.findSymbolsByName("clk_gen");
    CHECK(got.size() == 2);
}

TEST_CASE("findSymbolsByName returns empty for unknown name", "[db][symbol-db]") {
    Fixture f;
    CHECK(f.sdb.findSymbolsByName("no_such").empty());
}

TEST_CASE("symbol parent and detail stored correctly", "[db][symbol-db]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Port, "clk", 3, 0, "top", "input"}});

    auto got = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(got.size() == 1);
    CHECK(got[0].parent == "top");
    CHECK(got[0].detail == "input");
}

// ---------------------------------------------------------------------------
// Diagnostics storage and retrieval
// ---------------------------------------------------------------------------

TEST_CASE("replaceDiagnostics stores errors", "[db][symbol-db]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");

    f.sdb.replaceDiagnostics(fid, {{2, 5, "syntax error"}});

    auto got = f.sdb.diagnosticsForFile("/a.sv");
    REQUIRE(got.size() == 1);
    CHECK(got[0].line    == 2);
    CHECK(got[0].col     == 5);
    CHECK(got[0].message == "syntax error");
}

TEST_CASE("replaceDiagnostics replaces previous diagnostics", "[db][symbol-db]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");

    f.sdb.replaceDiagnostics(fid, {{1, 0, "err1"}, {2, 0, "err2"}});
    f.sdb.replaceDiagnostics(fid, {{3, 0, "err3"}});

    auto got = f.sdb.diagnosticsForFile("/a.sv");
    REQUIRE(got.size() == 1);
    CHECK(got[0].message == "err3");
}

TEST_CASE("diagnosticsForFile returns empty for clean file", "[db][symbol-db]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceDiagnostics(fid, {});
    CHECK(f.sdb.diagnosticsForFile("/a.sv").empty());
}

TEST_CASE("diagnosticsForFile returns empty for unknown path", "[db][symbol-db]") {
    Fixture f;
    CHECK(f.sdb.diagnosticsForFile("/none.sv").empty());
}

// ---------------------------------------------------------------------------
// Package imports
// ---------------------------------------------------------------------------

TEST_CASE("replaceImports stores wildcard import", "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"util_pkg", "*"}});
    // Verify indirectly via findSymbolsVisibleAt: after storing a symbol in
    // util_pkg, it should appear in completion for /a.sv.
    auto pkgFid = f.sdb.upsertFile("/util_pkg.sv", "h");
    f.sdb.replaceSymbols(pkgFid, {
        {ParseRecordKind::Class, "MyClass", 2, 0, "util_pkg", "", 5, "util_pkg"}
    });
    // Module in a.sv so scopeAtPosition can return a scope for line 10.
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });
    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto it = std::find_if(visible.begin(), visible.end(),
                           [](const SymbolRow& r){ return r.name == "MyClass"; });
    CHECK(it != visible.end());
}

TEST_CASE("replaceImports: wildcard import excludes other package symbols",
          "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    // Import only pkg_a::*, not pkg_b
    f.sdb.replaceImports(fid, {{"pkg_a", "*"}});

    auto fidA = f.sdb.upsertFile("/pkg_a.sv", "h");
    f.sdb.replaceSymbols(fidA, {
        {ParseRecordKind::Class, "Alpha", 2, 0, "pkg_a", "", 5, "pkg_a"}
    });
    auto fidB = f.sdb.upsertFile("/pkg_b.sv", "h");
    f.sdb.replaceSymbols(fidB, {
        {ParseRecordKind::Class, "Beta", 2, 0, "pkg_b", "", 5, "pkg_b"}
    });
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto hasAlpha = std::any_of(visible.begin(), visible.end(),
                                [](const SymbolRow& r){ return r.name == "Alpha"; });
    auto hasBeta  = std::any_of(visible.begin(), visible.end(),
                                [](const SymbolRow& r){ return r.name == "Beta"; });
    CHECK(hasAlpha);
    CHECK_FALSE(hasBeta);
}

TEST_CASE("replaceImports: specific import makes only that symbol visible",
          "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    // Specific import: only Foo, not Bar
    f.sdb.replaceImports(fid, {{"util_pkg", "Foo"}});

    auto pkgFid = f.sdb.upsertFile("/util_pkg.sv", "h");
    f.sdb.replaceSymbols(pkgFid, {
        {ParseRecordKind::Class, "Foo", 2, 0, "util_pkg", "", 4, "util_pkg"},
        {ParseRecordKind::Class, "Bar", 5, 0, "util_pkg", "", 7, "util_pkg"}
    });
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto hasFoo = std::any_of(visible.begin(), visible.end(),
                              [](const SymbolRow& r){ return r.name == "Foo"; });
    auto hasBar = std::any_of(visible.begin(), visible.end(),
                              [](const SymbolRow& r){ return r.name == "Bar"; });
    CHECK(hasFoo);
    CHECK_FALSE(hasBar);
}

// plan.md §6.30 step 1: a wildcard-imported package declared in the *same*
// file as the importer. Part 1 of findSymbolsVisibleAt only covers the
// cursor's own scope chain, and Part 2 used to exclude the cursor's own file
// for every cross-file scope, wildcard packages included -- so these rows
// were reachable by neither arm.
TEST_CASE("findSymbolsVisibleAt: wildcard import of a package declared in the same file",
          "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"same_pkg", "*"}});
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Package, "same_pkg", 1, 8, "", "", 3, ""},
        {ParseRecordKind::Class,   "SameCls",  2, 8, "same_pkg", "same_pkg", 2, "same_pkg"},
        {ParseRecordKind::Module,  "top",      5, 7, "", "", 9, ""},
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 7);
    auto n = std::count_if(visible.begin(), visible.end(),
                           [](const SymbolRow& r){ return r.name == "SameCls"; });
    CHECK(n == 1);
}

TEST_CASE("findSymbolsVisibleAt: same-file wildcard import still excludes other packages",
          "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"pkg_a", "*"}});
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Package, "pkg_a", 1, 8, "", "", 3, ""},
        {ParseRecordKind::Class,   "Alpha", 2, 8, "pkg_a", "pkg_a", 2, "pkg_a"},
        {ParseRecordKind::Package, "pkg_b", 4, 8, "", "", 6, ""},
        {ParseRecordKind::Class,   "Beta",  5, 8, "pkg_b", "pkg_b", 5, "pkg_b"},
        {ParseRecordKind::Module,  "top",   8, 7, "", "", 12, ""},
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto has = [&](const char* name) {
        return std::any_of(visible.begin(), visible.end(),
                           [&](const SymbolRow& r){ return r.name == name; });
    };
    CHECK(has("Alpha"));
    CHECK_FALSE(has("Beta"));
}

TEST_CASE("findSymbolsVisibleAt: a cursor inside a package that is also wildcard-imported by "
          "its own file sees each member once, not twice",
          "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"self_pkg", "*"}});
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Package, "self_pkg", 1, 8, "", "", 6, ""},
        {ParseRecordKind::Class,   "SelfCls",  2, 8, "self_pkg", "self_pkg", 2, "self_pkg"},
    });

    // Line 4 is inside self_pkg: SelfCls is reachable both through the
    // local scope chain (Part 1) and through the wildcard import.
    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 4);
    auto n = std::count_if(visible.begin(), visible.end(),
                           [](const SymbolRow& r){ return r.name == "SelfCls"; });
    CHECK(n == 1);
}

TEST_CASE("replaceImports replaces previous imports", "[db][symbol-db][import]") {
    Fixture f;
    auto fid    = f.sdb.upsertFile("/a.sv", "h");
    auto pkgFid = f.sdb.upsertFile("/pkg.sv", "h");
    f.sdb.replaceSymbols(pkgFid, {
        {ParseRecordKind::Class, "C", 2, 0, "pkg", "", 4, "pkg"}
    });
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });

    // Store wildcard import then replace with empty.
    f.sdb.replaceImports(fid, {{"pkg", "*"}});
    f.sdb.replaceImports(fid, {});

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto hasC = std::any_of(visible.begin(), visible.end(),
                            [](const SymbolRow& r){ return r.name == "C"; });
    CHECK_FALSE(hasC);
}

// ---------------------------------------------------------------------------
// Package exports — transitive visibility
// ---------------------------------------------------------------------------

TEST_CASE("plain import is not transitive: second-level import stays hidden",
          "[db][symbol-db][import][export]") {
    Fixture f;
    // a.sv imports pkg_a::* only.
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"pkg_a", "*"}});

    // pkg_a itself plainly imports pkg_b::* — no export, so this must not leak.
    auto fidA = f.sdb.upsertFile("/pkg_a.sv", "h");
    f.sdb.replaceSymbols(fidA, {
        {ParseRecordKind::Package, "pkg_a", 1, 0, "", "", 10, ""},
        {ParseRecordKind::Class,   "Alpha", 2, 0, "pkg_a", "", 5, "pkg_a"}
    });
    f.sdb.replaceImports(fidA, {{"pkg_b", "*"}});

    auto fidB = f.sdb.upsertFile("/pkg_b.sv", "h");
    f.sdb.replaceSymbols(fidB, {
        {ParseRecordKind::Package, "pkg_b", 1, 0, "", "", 10, ""},
        {ParseRecordKind::Class,   "Beta",  2, 0, "pkg_b", "", 5, "pkg_b"}
    });

    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto hasAlpha = std::any_of(visible.begin(), visible.end(),
                                [](const SymbolRow& r){ return r.name == "Alpha"; });
    auto hasBeta  = std::any_of(visible.begin(), visible.end(),
                                [](const SymbolRow& r){ return r.name == "Beta"; });
    CHECK(hasAlpha);      // the immediately imported scope is visible
    CHECK_FALSE(hasBeta); // its own (non-exported) import is not
}

TEST_CASE("export re-exports a wildcard-imported package transitively",
          "[db][symbol-db][import][export]") {
    Fixture f;
    // base_pkg declares Alpha.
    auto fidBase = f.sdb.upsertFile("/base_pkg.sv", "h");
    f.sdb.replaceSymbols(fidBase, {
        {ParseRecordKind::Package, "base_pkg", 1, 0, "", "", 10, ""},
        {ParseRecordKind::Class,   "Alpha",    2, 0, "base_pkg", "", 5, "base_pkg"}
    });

    // middle_pkg declares Beta, imports base_pkg::*, and re-exports it.
    auto fidMiddle = f.sdb.upsertFile("/middle_pkg.sv", "h");
    f.sdb.replaceSymbols(fidMiddle, {
        {ParseRecordKind::Package, "middle_pkg", 1, 0, "", "", 10, ""},
        {ParseRecordKind::Class,   "Beta",       2, 0, "middle_pkg", "", 5, "middle_pkg"}
    });
    f.sdb.replaceImports(fidMiddle, {
        {"base_pkg", "*", 0, "", false}, // import base_pkg::*;
        {"base_pkg", "*", 0, "", true}   // export base_pkg::*;
    });

    // a.sv imports middle_pkg::* only — never mentions base_pkg.
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"middle_pkg", "*"}});
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto hasBeta  = std::any_of(visible.begin(), visible.end(),
                                [](const SymbolRow& r){ return r.name == "Beta"; });
    auto hasAlpha = std::any_of(visible.begin(), visible.end(),
                                [](const SymbolRow& r){ return r.name == "Alpha"; });
    CHECK(hasBeta);   // middle_pkg's own symbol
    CHECK(hasAlpha);  // re-exported from base_pkg via export base_pkg::*
}

TEST_CASE("export of a specific symbol re-exports only that item",
          "[db][symbol-db][import][export]") {
    Fixture f;
    auto fidBase = f.sdb.upsertFile("/base_pkg.sv", "h");
    f.sdb.replaceSymbols(fidBase, {
        {ParseRecordKind::Package, "base_pkg", 1, 0, "", "", 10, ""},
        {ParseRecordKind::Class,   "Foo", 2, 0, "base_pkg", "", 4, "base_pkg"},
        {ParseRecordKind::Class,   "Bar", 5, 0, "base_pkg", "", 7, "base_pkg"}
    });

    auto fidMiddle = f.sdb.upsertFile("/middle_pkg.sv", "h");
    f.sdb.replaceSymbols(fidMiddle, {
        {ParseRecordKind::Package, "middle_pkg", 1, 0, "", "", 10, ""}
    });
    f.sdb.replaceImports(fidMiddle, {
        {"base_pkg", "Foo", 0, "", false}, // import base_pkg::Foo;
        {"base_pkg", "Foo", 0, "", true}   // export base_pkg::Foo;
    });

    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"middle_pkg", "*"}});
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""}
    });

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", 10);
    auto hasFoo = std::any_of(visible.begin(), visible.end(),
                              [](const SymbolRow& r){ return r.name == "Foo"; });
    auto hasBar = std::any_of(visible.begin(), visible.end(),
                              [](const SymbolRow& r){ return r.name == "Bar"; });
    CHECK(hasFoo);
    CHECK_FALSE(hasBar);
}

// ---------------------------------------------------------------------------
// Instantiations
// ---------------------------------------------------------------------------

TEST_CASE("replaceInstantiations stores records", "[db][symbol-db][instantiation]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/top.sv", "h");
    f.sdb.replaceInstantiations(fid, {{"sub", "u0", 3, ""}});

    // Indirect check via unresolvedInstantiatedTypeNames: with no "sub" symbol
    // declared anywhere, it must show up as unresolved.
    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "sub") != unresolved.end());
}

TEST_CASE("replaceInstantiations replaces previous instantiations",
          "[db][symbol-db][instantiation]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/top.sv", "h");
    f.sdb.replaceInstantiations(fid, {{"old_sub", "u0", 3, ""}});
    f.sdb.replaceInstantiations(fid, {{"new_sub", "u0", 3, ""}});

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "old_sub") == unresolved.end());
    CHECK(std::find(unresolved.begin(), unresolved.end(), "new_sub") != unresolved.end());
}

TEST_CASE("unresolvedInstantiatedTypeNames excludes declared-and-instantiated names",
          "[db][symbol-db][instantiation]") {
    Fixture f;
    auto fidTop = f.sdb.upsertFile("/top.sv", "h");
    auto fidSub = f.sdb.upsertFile("/sub.sv", "h");
    f.sdb.replaceSymbols(fidSub, {
        {ParseRecordKind::Module, "sub", 1, 0, "", "", 5, ""}
    });
    f.sdb.replaceInstantiations(fidTop, {{"sub", "u0", 3, ""}});

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "sub") == unresolved.end());
}

TEST_CASE("unresolvedInstantiatedTypeNames excludes names declared in a different file",
          "[db][symbol-db][instantiation]") {
    Fixture f;
    auto fidTop = f.sdb.upsertFile("/top.sv", "h");
    auto fidLib = f.sdb.upsertFile("/lib/sub.sv", "h");
    f.sdb.replaceSymbols(fidLib, {
        {ParseRecordKind::Interface, "bus_if", 1, 0, "", "", 5, ""}
    });
    f.sdb.replaceInstantiations(fidTop, {{"bus_if", "u_bus", 4, ""}});

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "bus_if") == unresolved.end());
}

TEST_CASE("appendDiagnostics adds without deleting existing diagnostics",
          "[db][symbol-db][instantiation]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/top.sv", "h");
    f.sdb.replaceDiagnostics(fid, {{1, 0, "existing error"}});
    f.sdb.appendDiagnostics(fid, {{3, 0, "unresolved instantiation of 'sub'"}});

    auto diags = f.sdb.diagnosticsForFile("/top.sv");
    REQUIRE(diags.size() == 2);
    auto hasExisting = std::any_of(diags.begin(), diags.end(),
                                   [](const DiagnosticRow& d){ return d.message == "existing error"; });
    auto hasNew = std::any_of(diags.begin(), diags.end(),
                              [](const DiagnosticRow& d){ return d.message.find("unresolved") != std::string::npos; });
    CHECK(hasExisting);
    CHECK(hasNew);
}

// ---------------------------------------------------------------------------
// scopeKindAtPosition (keyword-completion context, plan.md §6.9)
// ---------------------------------------------------------------------------

TEST_CASE("scopeKindAtPosition returns empty string outside any tracked scope",
          "[db][symbol-db][scope-kind]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Module, "top", 5, 0, "", "", 10, ""}
    });
    CHECK(f.sdb.scopeKindAtPosition("/a.sv", 1) == "");   // before the module
    CHECK(f.sdb.scopeKindAtPosition("/a.sv", 20) == "");  // after the module
}

TEST_CASE("scopeKindAtPosition returns the innermost enclosing kind",
          "[db][symbol-db][scope-kind]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Module,   "top",  1, 0,     "",    "", 20, ""},
        {ParseRecordKind::Class,    "Cls",  5, 0,     "top", "", 15, "top"},
        {ParseRecordKind::Function, "meth", 8, 0,     "Cls", "", 12, "top::Cls"},
    });
    CHECK(f.sdb.scopeKindAtPosition("/a.sv", 3)  == "Module");
    CHECK(f.sdb.scopeKindAtPosition("/a.sv", 6)  == "Class");
    CHECK(f.sdb.scopeKindAtPosition("/a.sv", 10) == "Function");
}

// ---------------------------------------------------------------------------
// enclosingClassNameAt (this/super resolution, plan.md §6.14)
// ---------------------------------------------------------------------------

TEST_CASE("enclosingClassNameAt finds the class directly containing a position",
          "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 0, "",    "", 20, ""},
        {ParseRecordKind::Class,  "Cls", 5, 0, "top", "", 15, "top"},
    });
    CHECK(f.sdb.enclosingClassNameAt("/a.sv", 6) == "Cls");
}

TEST_CASE("enclosingClassNameAt finds the enclosing class even nested inside one of its methods",
          "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Module,   "top",  1, 0, "",    "",        20, ""},
        {ParseRecordKind::Class,    "Cls",  5, 0, "top", "",        15, "top"},
        {ParseRecordKind::Function, "meth", 8, 0, "Cls", "void",    12, "top::Cls"},
    });
    // Line 10 is inside meth's body -- scopeKindAtPosition would report
    // "Function" there, but enclosingClassNameAt must still find "Cls".
    CHECK(f.sdb.enclosingClassNameAt("/a.sv", 10) == "Cls");
}

TEST_CASE("enclosingClassNameAt returns empty string outside any class",
          "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""},
    });
    CHECK(f.sdb.enclosingClassNameAt("/a.sv", 2) == "");
}

// ---------------------------------------------------------------------------
// baseClassChain / resolveMethod (plan.md §6.26 -- scope/type-aware
// method-call resolution, fixing the false positives §6.23's own
// UVM-corpus probe surfaced)
// ---------------------------------------------------------------------------

TEST_CASE("baseClassChain walks two extends levels outward", "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "Base",    1, 0, "", "",     5,  ""},
        {ParseRecordKind::Class, "Derived", 10, 0, "", "Base", 15, ""},
    });
    auto chain = f.sdb.baseClassChain("Derived", "/a.sv");
    REQUIRE(chain.size() == 2);
    CHECK(chain[0] == "Derived");
    CHECK(chain[1] == "Base");
}

TEST_CASE("baseClassChain returns empty for a name that isn't a known class",
          "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "Base", 1, 0, "", "", 5, ""},
    });
    CHECK(f.sdb.baseClassChain("type_id", "/a.sv").empty());
}

TEST_CASE("baseClassChain stops on a cycle rather than looping forever",
          "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "ClassX", 1,  0, "", "ClassY", 5,  ""},
        {ParseRecordKind::Class, "ClassY", 10, 0, "", "ClassX", 15, ""},
    });
    auto chain = f.sdb.baseClassChain("ClassX", "/a.sv");
    REQUIRE(chain.size() == 2);
    CHECK(chain[0] == "ClassX");
    CHECK(chain[1] == "ClassY");
}

TEST_CASE("resolveMethod finds a method on a distant ancestor, not a same-named "
          "method on an unrelated class", "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class,    "Other",   1,  0, "",      "",      5,  ""},
        {ParseRecordKind::Function, "helper",  2,  0, "Other", "void",  3,  "Other"},
        {ParseRecordKind::Class,    "Base",    10, 0, "",      "",      15, ""},
        {ParseRecordKind::Function, "helper",  11, 0, "Base",  "void",  12, "Base"},
        {ParseRecordKind::Class,    "Derived", 20, 0, "",      "Base",  25, ""},
    });
    auto found = f.sdb.resolveMethod("Derived", "helper", "/a.sv");
    REQUIRE(found.has_value());
    CHECK(found->scope == "Base");
}

TEST_CASE("resolveMethod returns nullopt for a name that isn't a known class",
          "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "Base", 1, 0, "", "", 5, ""},
    });
    CHECK_FALSE(f.sdb.resolveMethod("type_id", "get", "/a.sv").has_value());
}

TEST_CASE("resolveMethod returns nullopt when neither the class nor any ancestor "
          "declares the method", "[db][symbol-db][chain]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "Lonely", 1, 0, "", "", 5, ""},
    });
    CHECK_FALSE(f.sdb.resolveMethod("Lonely", "missing", "/a.sv").has_value());
}

// ---------------------------------------------------------------------------
// setLibraryIncludeDirs / libraryIncludeDirs (plan.md §6.19 piece 4 --
// baking a library DB's own build-time includeDirs into the DB file itself)
// ---------------------------------------------------------------------------

TEST_CASE("libraryIncludeDirs is empty before setLibraryIncludeDirs is ever called",
          "[db][symbol-db][library-include-dirs]") {
    Fixture f;
    CHECK(f.sdb.libraryIncludeDirs().empty());
}

TEST_CASE("setLibraryIncludeDirs then libraryIncludeDirs round-trips in order",
          "[db][symbol-db][library-include-dirs]") {
    Fixture f;
    f.sdb.setLibraryIncludeDirs({"/a/dir", "/b/dir", "/c/dir"});
    CHECK(f.sdb.libraryIncludeDirs() == std::vector<std::string>{"/a/dir", "/b/dir", "/c/dir"});
}

TEST_CASE("setLibraryIncludeDirs overwrites whatever was stored before",
          "[db][symbol-db][library-include-dirs]") {
    Fixture f;
    f.sdb.setLibraryIncludeDirs({"/old/dir"});
    f.sdb.setLibraryIncludeDirs({"/new/dir1", "/new/dir2"});
    CHECK(f.sdb.libraryIncludeDirs() == std::vector<std::string>{"/new/dir1", "/new/dir2"});
}

TEST_CASE("setLibraryIncludeDirs with an empty list clears any previously stored dirs",
          "[db][symbol-db][library-include-dirs]") {
    Fixture f;
    f.sdb.setLibraryIncludeDirs({"/old/dir"});
    f.sdb.setLibraryIncludeDirs({});
    CHECK(f.sdb.libraryIncludeDirs().empty());
}

TEST_CASE("libraryIncludeDirs returns empty, not throws, on a DB predating this feature",
          "[db][symbol-db][library-include-dirs]") {
    // Simulates a library DB built before schema v6 added this table --
    // must be treated as "no includeDirs to contribute," not an error, so a
    // project referencing an older prebuilt DB keeps working exactly as it
    // did before this feature existed.
    Fixture f;
    f.db.execute("DROP TABLE library_include_dirs");
    CHECK(f.sdb.libraryIncludeDirs().empty());
}

// ---------------------------------------------------------------------------
// setBuiltByVersion / builtByVersion / resetAllFiles (stale-cache guard: a
// library DB records which svlsp build produced it, so LibraryDbBuilder::
// build can detect a different binary and force a full rebuild instead of
// trusting per-file content hashes that can't see the parser itself changed)
// ---------------------------------------------------------------------------

TEST_CASE("builtByVersion is empty before setBuiltByVersion is ever called",
          "[db][symbol-db][built-by-version]") {
    Fixture f;
    CHECK(f.sdb.builtByVersion() == "");
}

TEST_CASE("setBuiltByVersion then builtByVersion round-trips",
          "[db][symbol-db][built-by-version]") {
    Fixture f;
    f.sdb.setBuiltByVersion("abc1234");
    CHECK(f.sdb.builtByVersion() == "abc1234");
}

TEST_CASE("setBuiltByVersion overwrites whatever was stored before",
          "[db][symbol-db][built-by-version]") {
    Fixture f;
    f.sdb.setBuiltByVersion("abc1234");
    f.sdb.setBuiltByVersion("def5678-dirty");
    CHECK(f.sdb.builtByVersion() == "def5678-dirty");
}

TEST_CASE("builtByVersion returns empty, not throws, on a DB predating this feature",
          "[db][symbol-db][built-by-version]") {
    // Simulates a library DB built before schema v7 added this table --
    // must be treated as "unknown version," not an error, matching
    // libraryIncludeDirs's own precedent for the same "predates this
    // feature" case.
    Fixture f;
    f.db.execute("DROP TABLE library_build_info");
    CHECK(f.sdb.builtByVersion() == "");
}

TEST_CASE("resetAllFiles clears files and cascades to symbols/diagnostics",
          "[db][symbol-db][built-by-version]") {
    Fixture f;
    auto fileId = f.sdb.upsertFile("/a.sv", "hash1");
    f.sdb.replaceSymbols(fileId, {{ParseRecordKind::Module, "top", 1, 0, "", "", 5}});
    f.sdb.replaceDiagnostics(fileId, {{1, 0, "oops"}});

    f.sdb.resetAllFiles();

    CHECK(f.sdb.getFileHash("/a.sv") == "");
    CHECK(f.sdb.symbolsForFile("/a.sv").empty());
    CHECK(f.sdb.diagnosticsForFile("/a.sv").empty());
}

// ---------------------------------------------------------------------------
// replaceFileIncludes / includersOf -- plan.md §6.4, cross-file invalidation
// ---------------------------------------------------------------------------

TEST_CASE("includersOf is empty before anything is recorded", "[db][symbol-db][includes]") {
    Fixture f;
    f.sdb.upsertFile("/inc.sv", "");
    CHECK(f.sdb.includersOf("/inc.sv").empty());
}

TEST_CASE("includersOf returns the includer after replaceFileIncludes",
          "[db][symbol-db][includes]") {
    Fixture f;
    auto topId = f.sdb.upsertFile("/top.sv", "hash1");
    f.sdb.upsertFile("/inc.sv", "");

    f.sdb.replaceFileIncludes(topId, {"/inc.sv"});

    auto includers = f.sdb.includersOf("/inc.sv");
    REQUIRE(includers.size() == 1);
    CHECK(includers[0] == "/top.sv");
}

TEST_CASE("includersOf returns every top-level file that includes the same file",
          "[db][symbol-db][includes]") {
    Fixture f;
    auto topAId = f.sdb.upsertFile("/top_a.sv", "hash1");
    auto topBId = f.sdb.upsertFile("/top_b.sv", "hash2");
    f.sdb.upsertFile("/shared.svh", "");

    f.sdb.replaceFileIncludes(topAId, {"/shared.svh"});
    f.sdb.replaceFileIncludes(topBId, {"/shared.svh"});

    auto includers = f.sdb.includersOf("/shared.svh");
    CHECK(includers.size() == 2);
    CHECK(std::find(includers.begin(), includers.end(), "/top_a.sv") != includers.end());
    CHECK(std::find(includers.begin(), includers.end(), "/top_b.sv") != includers.end());
}

TEST_CASE("replaceFileIncludes overwrites a file's previous include set",
          "[db][symbol-db][includes]") {
    Fixture f;
    auto topId = f.sdb.upsertFile("/top.sv", "hash1");
    f.sdb.upsertFile("/old_inc.sv", "");
    f.sdb.upsertFile("/new_inc.sv", "");

    f.sdb.replaceFileIncludes(topId, {"/old_inc.sv"});
    f.sdb.replaceFileIncludes(topId, {"/new_inc.sv"});

    CHECK(f.sdb.includersOf("/old_inc.sv").empty());
    REQUIRE(f.sdb.includersOf("/new_inc.sv").size() == 1);
    CHECK(f.sdb.includersOf("/new_inc.sv")[0] == "/top.sv");
}

TEST_CASE("includersOf returns empty for a path with no known file row",
          "[db][symbol-db][includes]") {
    Fixture f;
    CHECK(f.sdb.includersOf("/never_seen.sv").empty());
}

// plan.md §6.30 step A: class names reaching baseClassChain may now carry a
// `pkg::`/`Outer::`/`$unit::` qualifier (userTypeName and a class's own
// `extends` detail keep it). A qualified name must pick the class in exactly
// that scope, never a same-named class elsewhere.
TEST_CASE("baseClassChain resolves a package-qualified name to that package's class",
          "[db][symbol-db][chain][qualified]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/pkgs.sv", "h"), {
        {ParseRecordKind::Class, "Item", 2, 8, "pkg_a", "", 3, "pkg_a"},
        {ParseRecordKind::Class, "Item", 6, 8, "pkg_b", "", 7, "pkg_b"},
    });
    auto chain = f.sdb.baseClassChain("pkg_b::Item", "/top.sv");
    REQUIRE(chain.size() == 1);
    CHECK(chain[0] == "pkg_b::Item");
}

TEST_CASE("baseClassChain follows a package-qualified extends into the right package",
          "[db][symbol-db][chain][qualified]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/pkgs.sv", "h"), {
        {ParseRecordKind::Class, "Base",  2, 8, "pkg_a", "",             3,  "pkg_a"},
        {ParseRecordKind::Class, "Base",  6, 8, "pkg_b", "",             7,  "pkg_b"},
        {ParseRecordKind::Class, "Child", 9, 6, "",      "pkg_b::Base", 10, ""},
    });
    auto chain = f.sdb.baseClassChain("Child", "/pkgs.sv");
    REQUIRE(chain.size() == 2);
    CHECK(chain[1] == "pkg_b::Base");
}

TEST_CASE("baseClassChain resolves a nested-class and a $unit-qualified name",
          "[db][symbol-db][chain][qualified]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "Outer", 1, 8, "p",        "", 6, "p"},
        {ParseRecordKind::Class, "Inner", 2, 10, "Outer",   "", 3, "p::Outer"},
        {ParseRecordKind::Class, "Inner", 8, 6, "",         "", 9, ""},
    });
    auto nested = f.sdb.baseClassChain("p::Outer::Inner", "/a.sv");
    REQUIRE(nested.size() == 1);
    CHECK(nested[0] == "p::Outer::Inner");
    auto unit = f.sdb.baseClassChain("$unit::Inner", "/a.sv");
    REQUIRE(unit.size() == 1);
    CHECK(unit[0] == "Inner");
}

TEST_CASE("baseClassChain returns empty for a qualified name whose scope has no such class",
          "[db][symbol-db][chain][qualified]") {
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"), {
        {ParseRecordKind::Class, "Item", 2, 8, "pkg_a", "", 3, "pkg_a"},
    });
    CHECK(f.sdb.baseClassChain("pkg_b::Item", "/a.sv").empty());
}

TEST_CASE("importsForFile returns a file's imports and {} for an unknown path",
          "[db][symbol-db][import]") {
    Fixture f;
    auto fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceImports(fid, {{"pkg_a", "*"}, {"pkg_b", "Item"}});
    auto imps = f.sdb.importsForFile("/a.sv");
    REQUIRE(imps.size() == 2);
    CHECK(std::any_of(imps.begin(), imps.end(),
                      [](const ImportRow& i){ return i.pkgName == "pkg_a" && i.item == "*"; }));
    CHECK(std::any_of(imps.begin(), imps.end(),
                      [](const ImportRow& i){ return i.pkgName == "pkg_b" && i.item == "Item"; }));
    CHECK(f.sdb.importsForFile("/none.sv").empty());
}
