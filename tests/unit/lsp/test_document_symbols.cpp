#include <catch2/catch_test_macros.hpp>
#include "lsp/document_symbols.h"

static lsp::DocumentSymbolParams makeParams(std::string_view path)
{
    lsp::DocumentSymbolParams p;
    p.textDocument.uri = lsp::DocumentUri::fromPath(path);
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("DocumentSymbolsProvider: returns null for a basic fixture", "[document_symbols]")
{
    auto result = DocumentSymbolsProvider::getDocumentSymbols(makeParams("/tmp/test.sv"));
    REQUIRE(result.isNull());
}

TEST_CASE("DocumentSymbolsProvider: returns null for any URI", "[document_symbols]")
{
    auto r1 = DocumentSymbolsProvider::getDocumentSymbols(makeParams("/proj/a.sv"));
    auto r2 = DocumentSymbolsProvider::getDocumentSymbols(makeParams("/proj/b.sv"));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
