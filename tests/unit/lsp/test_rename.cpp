#include <catch2/catch_test_macros.hpp>
#include "lsp/rename.h"

static lsp::RenameParams makeParams(std::string_view path, unsigned line, unsigned col,
                                     std::string_view newName)
{
    lsp::RenameParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    p.newName            = newName;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("RenameProvider: returns null at position (0,0)", "[rename]")
{
    auto result = RenameProvider::getRename(makeParams("/tmp/test.sv", 0, 0, "new_name"));
    REQUIRE(result.isNull());
}

TEST_CASE("RenameProvider: returns null at arbitrary position", "[rename]")
{
    auto result = RenameProvider::getRename(makeParams("/tmp/test.sv", 10, 5, "renamed"));
    REQUIRE(result.isNull());
}

TEST_CASE("RenameProvider: returns null regardless of new name", "[rename]")
{
    auto r1 = RenameProvider::getRename(makeParams("/proj/a.sv", 0, 0, "foo"));
    auto r2 = RenameProvider::getRename(makeParams("/proj/a.sv", 0, 0, "very_long_new_signal_name"));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
