#include <catch2/catch_test_macros.hpp>
#include "lsp/macro_resolution.h"

// plan.md §6.29 follow-up: which identifier positions are macro names.

namespace {

// The macro name at (line 0, col) of `text`, or "<none>".
std::string nameAt(const std::string& text, unsigned col, unsigned line = 0)
{
    auto m = macroNameAt(text, line, col);
    return m ? m->name : "<none>";
}

} // namespace

TEST_CASE("macroNameAt: a backtick use, cursor on the name or on the backtick",
          "[macro][phase6.29]")
{
    const std::string text = "x = `FOO(a) + `BAR;";
    CHECK(nameAt(text, 5) == "FOO");
    CHECK(nameAt(text, 7) == "FOO");
    CHECK(nameAt(text, 4) == "FOO"); // on the backtick
    CHECK(nameAt(text, 15) == "BAR");
    CHECK(nameAt(text, 9) == "<none>"); // `a`, a plain identifier

    auto m = macroNameAt(text, 0, 7);
    REQUIRE(m.has_value());
    CHECK(m->line == 0);
    CHECK(m->character == 5); // the name's first character, not the backtick
}

TEST_CASE("macroNameAt: the name operand of define/undef/ifdef/ifndef/elsif",
          "[macro][phase6.29]")
{
    CHECK(nameAt("`define FOO 1", 8) == "FOO");
    CHECK(nameAt("  `define FOO(A) A", 11) == "FOO");
    CHECK(nameAt("`undef FOO", 7) == "FOO");
    CHECK(nameAt("`ifdef FOO", 7) == "FOO");
    CHECK(nameAt("`ifndef FOO", 8) == "FOO");
    CHECK(nameAt("`elsif FOO", 7) == "FOO");
    CHECK(nameAt("x\n`ifdef   FOO\ny", 11, 1) == "FOO");
}

TEST_CASE("macroNameAt: not a macro name", "[macro][phase6.29]")
{
    // The directive keyword itself.
    CHECK(nameAt("`define FOO 1", 2) == "<none>");
    CHECK(nameAt("`include \"f.svh\"", 3) == "<none>");
    CHECK(nameAt("`timescale 1ns/1ps", 3) == "<none>");
    // A plain identifier.
    CHECK(nameAt("x = FOO;", 5) == "<none>");
    // A parameter used in a define body.
    CHECK(nameAt("`define M(A) A + 1", 13) == "<none>");
    // The define's value.
    CHECK(nameAt("`define W WIDTH", 11) == "<none>");
    // Inside a comment or a string.
    CHECK(nameAt("x; // `FOO", 7) == "<none>");
    CHECK(nameAt("s = \"`FOO\";", 7) == "<none>");
    // Whitespace.
    CHECK(nameAt("x = `FOO ;", 8) == "<none>");
}

TEST_CASE("isMacroOccurrence classifies a found identifier start", "[macro][phase6.29]")
{
    const std::string text = "`define FOO 1\nx = `FOO + FOO;\n";
    CHECK(isMacroOccurrence(text, 8));              // `define FOO
    CHECK(isMacroOccurrence(text, 14 + 5));         // `FOO
    CHECK_FALSE(isMacroOccurrence(text, 14 + 11));  // plain FOO
}

TEST_CASE("macroSignature renders parameters and defaults", "[macro][phase6.29]")
{
    MacroRow fn{"M", 1, 8, true, {"A", "B"}, {std::nullopt, std::string("1")}, "/m.svh", "A+B"};
    CHECK(macroSignature(fn) == "`M(A, B = 1)");
    MacroRow obj{"W", 1, 8, false, {}, {}, "/m.svh", "8"};
    CHECK(macroSignature(obj) == "`W");
    MacroRow empty{"Z", 1, 8, true, {}, {}, "/m.svh", "0"};
    CHECK(macroSignature(empty) == "`Z()");
}
