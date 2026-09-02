#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"

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
