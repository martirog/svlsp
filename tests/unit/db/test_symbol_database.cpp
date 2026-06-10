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
