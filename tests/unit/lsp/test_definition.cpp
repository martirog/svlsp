#include <catch2/catch_test_macros.hpp>
#include "lsp/definition.h"

static lsp::DefinitionParams makeParams(std::string_view path, unsigned line, unsigned col)
{
    lsp::DefinitionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("DefinitionProvider: returns null at position (0,0)", "[definition]")
{
    auto result = DefinitionProvider::getDefinition(makeParams("/tmp/test.sv", 0, 0));
    REQUIRE(result.isNull());
}

TEST_CASE("DefinitionProvider: returns null at arbitrary position", "[definition]")
{
    auto result = DefinitionProvider::getDefinition(makeParams("/tmp/test.sv", 10, 5));
    REQUIRE(result.isNull());
}

TEST_CASE("DefinitionProvider: returns null for any URI", "[definition]")
{
    auto r1 = DefinitionProvider::getDefinition(makeParams("/proj/a.sv",  0,  0));
    auto r2 = DefinitionProvider::getDefinition(makeParams("/proj/b.sv", 99, 42));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
