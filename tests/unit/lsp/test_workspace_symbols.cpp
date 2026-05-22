#include <catch2/catch_test_macros.hpp>
#include "lsp/workspace_symbols.h"

static lsp::WorkspaceSymbolParams makeParams(std::string_view query)
{
    lsp::WorkspaceSymbolParams p;
    p.query = query;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("WorkspaceSymbolsProvider: returns null for empty query", "[workspace_symbols]")
{
    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams(""));
    REQUIRE(result.isNull());
}

TEST_CASE("WorkspaceSymbolsProvider: returns null for non-empty query", "[workspace_symbols]")
{
    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams("my_module"));
    REQUIRE(result.isNull());
}
