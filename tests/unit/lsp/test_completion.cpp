#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"

static lsp::CompletionParams makeParams(std::string_view path, unsigned line, unsigned col)
{
    lsp::CompletionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: returns null at position (0,0)", "[completion]")
{
    auto result = CompletionProvider::getCompletion(makeParams("/tmp/test.sv", 0, 0));
    REQUIRE(result.isNull());
}

TEST_CASE("CompletionProvider: returns null at arbitrary position", "[completion]")
{
    auto result = CompletionProvider::getCompletion(makeParams("/tmp/test.sv", 10, 5));
    REQUIRE(result.isNull());
}

TEST_CASE("CompletionProvider: returns null for any URI", "[completion]")
{
    auto r1 = CompletionProvider::getCompletion(makeParams("/proj/a.sv",  0,  0));
    auto r2 = CompletionProvider::getCompletion(makeParams("/proj/b.sv", 99, 42));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
