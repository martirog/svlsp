#include <catch2/catch_test_macros.hpp>
#include "lsp/document_symbols.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"

namespace {

struct Fixture {
    Database      db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }

    int64_t addFile(const std::string& path) {
        return sdb.upsertFile(path, "hash");
    }
    void addSymbols(int64_t fid, const std::vector<ParseRecord>& recs) {
        sdb.replaceSymbols(fid, recs);
    }
};

lsp::DocumentSymbolParams makeParams(std::string_view path) {
    lsp::DocumentSymbolParams p;
    p.textDocument.uri = lsp::DocumentUri::fromPath(path);
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// Phase 6.1 — document symbols backed by SymbolDatabase
// ---------------------------------------------------------------------------

TEST_CASE("DocumentSymbolsProvider: null when file not in DB", "[document_symbols]")
{
    Fixture f;
    auto result = DocumentSymbolsProvider::getDocumentSymbols(
        makeParams("/tmp/unknown.sv"), f.sdb);
    REQUIRE(result.isNull());
}

TEST_CASE("DocumentSymbolsProvider: returns symbols for open file", "[document_symbols]")
{
    Fixture f;
    const std::string path = "/tmp/adder.sv";
    int64_t fid = f.addFile(path);
    f.addSymbols(fid, {
        {ParseRecordKind::Module,    "adder",  4, 7,  "",      "", 12, ""},
        {ParseRecordKind::Parameter, "WIDTH",  5, 18, "adder", "", 0,  "adder"},
        {ParseRecordKind::Port,      "clk",    7, 10, "adder", "input", 0, "adder"},
    });

    auto result = DocumentSymbolsProvider::getDocumentSymbols(makeParams(path), f.sdb);
    REQUIRE_FALSE(result.isNull());

    auto& syms = result.get<lsp::Array<lsp::DocumentSymbol>>();
    REQUIRE(syms.size() == 3);

    // Module kind check
    bool hasAdder = false;
    for (const auto& s : syms) {
        if (s.name == "adder") {
            hasAdder = true;
            CHECK(static_cast<lsp::SymbolKind>(s.kind) == lsp::SymbolKind::Module);
            // end_line 12 → LSP range end line = 11 (0-based)
            CHECK(s.range.end.line == 11u);
            // selection range: line 4 → LSP line 3
            CHECK(s.selectionRange.start.line == 3u);
            CHECK(s.selectionRange.start.character == 7u);
        }
    }
    CHECK(hasAdder);
}

TEST_CASE("DocumentSymbolsProvider: leaf symbols get single-line range", "[document_symbols]")
{
    Fixture f;
    const std::string path = "/tmp/signals.sv";
    int64_t fid = f.addFile(path);
    f.addSymbols(fid, {
        {ParseRecordKind::Signal, "data", 10, 14, "my_mod", "", 0, "my_mod"},
    });

    auto result = DocumentSymbolsProvider::getDocumentSymbols(makeParams(path), f.sdb);
    REQUIRE_FALSE(result.isNull());

    auto& syms = result.get<lsp::Array<lsp::DocumentSymbol>>();
    REQUIRE(syms.size() == 1);
    const auto& s = syms[0];
    CHECK(s.name == "data");
    CHECK(static_cast<lsp::SymbolKind>(s.kind) == lsp::SymbolKind::Variable);
    // Leaf: range == selectionRange
    CHECK(s.range.start.line      == s.selectionRange.start.line);
    CHECK(s.range.start.character == s.selectionRange.start.character);
    CHECK(s.range.end.line        == s.selectionRange.end.line);
    CHECK(s.range.end.character   == s.selectionRange.end.character);
}

TEST_CASE("DocumentSymbolsProvider: detail field populated when present", "[document_symbols]")
{
    Fixture f;
    const std::string path = "/tmp/detail.sv";
    int64_t fid = f.addFile(path);
    f.addSymbols(fid, {
        {ParseRecordKind::Port, "clk", 5, 4, "top", "input", 0, "top"},
    });

    auto result = DocumentSymbolsProvider::getDocumentSymbols(makeParams(path), f.sdb);
    auto& syms = result.get<lsp::Array<lsp::DocumentSymbol>>();
    REQUIRE(syms.size() == 1);
    REQUIRE(syms[0].detail.has_value());
    CHECK(*syms[0].detail == "input");
}
