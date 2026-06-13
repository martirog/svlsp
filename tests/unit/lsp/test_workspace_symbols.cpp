#include <catch2/catch_test_macros.hpp>
#include "lsp/workspace_symbols.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <algorithm>

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::WorkspaceSymbolParams makeParams(std::string_view query) {
    lsp::WorkspaceSymbolParams p;
    p.query = query;
    return p;
}

} // namespace

TEST_CASE("WorkspaceSymbolsProvider: null when DB has no symbols", "[workspace_symbols]")
{
    Fixture f;
    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams("adder"), f.sdb);
    REQUIRE(result.isNull());
}

TEST_CASE("WorkspaceSymbolsProvider: null when prefix matches nothing", "[workspace_symbols]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Module, "adder", 1, 0, "", "", 10, ""}});

    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams("xyz"), f.sdb);
    REQUIRE(result.isNull());
}

TEST_CASE("WorkspaceSymbolsProvider: returns matching symbols by prefix", "[workspace_symbols]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/a.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module,    "adder",   1, 0, "",      "", 10, ""},
        {ParseRecordKind::Module,    "arbiter", 20, 0, "",      "", 30, ""},
        {ParseRecordKind::Parameter, "WIDTH",    5, 4, "adder", "", 0,  "adder"},
    });

    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams("a"), f.sdb);
    REQUIRE_FALSE(result.isNull());

    auto& syms = result.get<lsp::Array<lsp::WorkspaceSymbol>>();
    // "adder" and "arbiter" start with "a"; "WIDTH" doesn't
    REQUIRE(syms.size() == 2);
    bool foundAdder   = std::any_of(syms.begin(), syms.end(), [](const auto& s){ return s.name == "adder"; });
    bool foundArbiter = std::any_of(syms.begin(), syms.end(), [](const auto& s){ return s.name == "arbiter"; });
    CHECK(foundAdder);
    CHECK(foundArbiter);
}

TEST_CASE("WorkspaceSymbolsProvider: empty query returns all symbols", "[workspace_symbols]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/b.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module,   "foo", 1, 0, "", "", 5, ""},
        {ParseRecordKind::Function, "bar", 7, 0, "foo", "void", 9, "foo"},
    });

    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams(""), f.sdb);
    REQUIRE_FALSE(result.isNull());

    auto& syms = result.get<lsp::Array<lsp::WorkspaceSymbol>>();
    CHECK(syms.size() == 2);
}

TEST_CASE("WorkspaceSymbolsProvider: location points to correct file and line", "[workspace_symbols]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/mod.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Module, "top", 3, 7, "", "", 20, ""}});

    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams("top"), f.sdb);
    REQUIRE_FALSE(result.isNull());

    auto& syms = result.get<lsp::Array<lsp::WorkspaceSymbol>>();
    REQUIRE(syms.size() == 1);

    // Location should be a proper Location (not a URI-only location)
    REQUIRE(std::holds_alternative<lsp::Location>(syms[0].location));
    const auto& loc = std::get<lsp::Location>(syms[0].location);
    CHECK(loc.range.start.line == 2u);      // 1-based line 3 → 0-based 2
    CHECK(loc.range.start.character == 7u);
}
