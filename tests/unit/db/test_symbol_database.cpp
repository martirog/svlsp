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
