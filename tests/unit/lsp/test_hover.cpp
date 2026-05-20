#include <catch2/catch_test_macros.hpp>
#include "lsp/hover.h"

static lsp::HoverParams makeParams(std::string_view path, unsigned line, unsigned col)
{
    lsp::HoverParams p;
    p.textDocument.uri = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("HoverProvider: returns null at position (0,0)", "[hover]")
{
    auto result = HoverProvider::getHover(makeParams("/tmp/test.sv", 0, 0));
    REQUIRE(result.isNull());
}

TEST_CASE("HoverProvider: returns null at arbitrary position", "[hover]")
{
    auto result = HoverProvider::getHover(makeParams("/tmp/test.sv", 10, 5));
    REQUIRE(result.isNull());
}

TEST_CASE("HoverProvider: returns null for any URI", "[hover]")
{
    auto r1 = HoverProvider::getHover(makeParams("/proj/a.sv",   0,  0));
    auto r2 = HoverProvider::getHover(makeParams("/proj/b.sv",  99, 42));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
