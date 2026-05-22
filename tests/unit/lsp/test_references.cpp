#include <catch2/catch_test_macros.hpp>
#include "lsp/references.h"

static lsp::ReferenceParams makeParams(std::string_view path, unsigned line, unsigned col,
                                        bool includeDeclaration = false)
{
    lsp::ReferenceParams p;
    p.textDocument.uri      = lsp::DocumentUri::fromPath(path);
    p.position.line         = line;
    p.position.character    = col;
    p.context.includeDeclaration = includeDeclaration;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("ReferencesProvider: returns null at position (0,0)", "[references]")
{
    auto result = ReferencesProvider::getReferences(makeParams("/tmp/test.sv", 0, 0));
    REQUIRE(result.isNull());
}

TEST_CASE("ReferencesProvider: returns null at arbitrary position", "[references]")
{
    auto result = ReferencesProvider::getReferences(makeParams("/tmp/test.sv", 10, 5));
    REQUIRE(result.isNull());
}

TEST_CASE("ReferencesProvider: returns null regardless of includeDeclaration", "[references]")
{
    auto r1 = ReferencesProvider::getReferences(makeParams("/proj/a.sv", 0, 0, false));
    auto r2 = ReferencesProvider::getReferences(makeParams("/proj/a.sv", 0, 0, true));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
