#include <catch2/catch_test_macros.hpp>
#include "lsp/signature_help.h"

static lsp::SignatureHelpParams makeParams(std::string_view path, unsigned line, unsigned col)
{
    lsp::SignatureHelpParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// ---------------------------------------------------------------------------
// Phase 3: always returns null (no symbol DB yet)
// ---------------------------------------------------------------------------

TEST_CASE("SignatureHelpProvider: returns null at position (0,0)", "[signature_help]")
{
    auto result = SignatureHelpProvider::getSignatureHelp(makeParams("/tmp/test.sv", 0, 0));
    REQUIRE(result.isNull());
}

TEST_CASE("SignatureHelpProvider: returns null at arbitrary position", "[signature_help]")
{
    auto result = SignatureHelpProvider::getSignatureHelp(makeParams("/tmp/test.sv", 10, 5));
    REQUIRE(result.isNull());
}

TEST_CASE("SignatureHelpProvider: returns null for any URI", "[signature_help]")
{
    auto r1 = SignatureHelpProvider::getSignatureHelp(makeParams("/proj/a.sv",  0,  0));
    auto r2 = SignatureHelpProvider::getSignatureHelp(makeParams("/proj/b.sv", 99, 42));
    REQUIRE(r1.isNull());
    REQUIRE(r2.isNull());
}
