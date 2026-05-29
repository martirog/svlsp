#include <catch2/catch_test_macros.hpp>
#include "compiler/compiler_directive_stripper.h"

// ---------------------------------------------------------------------------
// Compiler directives stripped and recorded
// ---------------------------------------------------------------------------

TEST_CASE("timescale stripped and recorded", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`timescale 1ns/1ps\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::Timescale);
    CHECK(dirs[0].value == "1ns/1ps");
    CHECK(dirs[0].line == 1);
}

TEST_CASE("default_nettype stripped and recorded", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`default_nettype none\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::DefaultNettype);
    CHECK(dirs[0].value == "none");
    CHECK(dirs[0].line == 1);
}

TEST_CASE("celldefine and endcelldefine stripped", "[compiler][stripper]") {
    std::string src = "`celldefine\nmodule m; endmodule\n`endcelldefine\n";
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "f.sv");
    REQUIRE(out == "\nmodule m; endmodule\n\n");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0].kind == DirectiveKind::Celldefine);
    CHECK(dirs[0].value.empty());
    CHECK(dirs[0].line == 1);
    CHECK(dirs[1].kind == DirectiveKind::Endcelldefine);
    CHECK(dirs[1].line == 3);
}

TEST_CASE("unconnected_drive stripped and recorded", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`unconnected_drive pull1\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::UnconnectedDrive);
    CHECK(dirs[0].value == "pull1");
}

TEST_CASE("nounconnected_drive stripped", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`nounconnected_drive\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::NounconnectedDrive);
    CHECK(dirs[0].value.empty());
}

TEST_CASE("resetall stripped", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`resetall\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::Resetall);
    CHECK(dirs[0].value.empty());
}

TEST_CASE("begin_keywords and end_keywords stripped", "[compiler][stripper]") {
    std::string src = "`begin_keywords \"1800-2017\"\nmodule m; endmodule\n`end_keywords\n";
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "f.sv");
    REQUIRE(out == "\nmodule m; endmodule\n\n");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0].kind == DirectiveKind::BeginKeywords);
    CHECK(dirs[0].value == "\"1800-2017\"");
    CHECK(dirs[1].kind == DirectiveKind::EndKeywords);
    CHECK(dirs[1].value.empty());
}

TEST_CASE("pragma stripped and recorded", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`pragma protect begin\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::Pragma);
    CHECK(dirs[0].value == "protect begin");
}

TEST_CASE("line directive stripped and recorded", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`line 42 \"other.sv\" 1\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::Line);
    CHECK(dirs[0].value == "42 \"other.sv\" 1");
}

// ---------------------------------------------------------------------------
// __FILE__ and __LINE__ substitution
// ---------------------------------------------------------------------------

TEST_CASE("__FILE__ replaced with quoted filepath", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("wire a = `__FILE__;\n", "/src/foo.sv");
    REQUIRE(out == "wire a = \"/src/foo.sv\";\n");
    REQUIRE(dirs.empty());
}

TEST_CASE("__FILE__ uses the filepath argument not a temp-buffer path", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("`__FILE__\n", "original.sv");
    CHECK(out.find("original.sv") != std::string::npos);
}

TEST_CASE("__LINE__ replaced with decimal line number", "[compiler][stripper]") {
    std::string src = "line1\nwire a = `__LINE__;\nline3\n";
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "f.sv");
    REQUIRE(out == "line1\nwire a = 2;\nline3\n");
    REQUIRE(dirs.empty());
}

TEST_CASE("__LINE__ reflects original source line even after prior directive removal", "[compiler][stripper]") {
    std::string src =
        "`timescale 1ns/1ps\n"   // line 1 — stripped to blank
        "wire a = `__LINE__;\n"; // line 2 — __LINE__ should be 2
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "f.sv");
    REQUIRE(out == "\nwire a = 2;\n");
}

TEST_CASE("__FILE__ and __LINE__ both substituted in the same line", "[compiler][stripper]") {
    std::string src = "$display(\"%s:%0d\", `__FILE__, `__LINE__);\n";
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "top.sv");
    REQUIRE(out == "$display(\"%s:%0d\", \"top.sv\", 1);\n");
    REQUIRE(dirs.empty());
}

// ---------------------------------------------------------------------------
// Pass-through and multi-line
// ---------------------------------------------------------------------------

TEST_CASE("non-directive lines pass through unchanged", "[compiler][stripper]") {
    std::string src = "module top;\n  wire a;\nendmodule\n";
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "f.sv");
    REQUIRE(out == src);
    REQUIRE(dirs.empty());
}

TEST_CASE("multiline: directives replaced with blank lines preserving line numbers", "[compiler][stripper]") {
    std::string src =
        "module top;\n"          // line 1
        "`timescale 1ns/1ps\n"   // line 2 — stripped
        "  wire a;\n"            // line 3
        "`default_nettype none\n"// line 4 — stripped
        "endmodule\n";           // line 5
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "top.sv");
    REQUIRE(out ==
        "module top;\n"
        "\n"
        "  wire a;\n"
        "\n"
        "endmodule\n");
    REQUIRE(dirs.size() == 2);
    CHECK(dirs[0].kind == DirectiveKind::Timescale);
    CHECK(dirs[0].line == 2);
    CHECK(dirs[1].kind == DirectiveKind::DefaultNettype);
    CHECK(dirs[1].line == 4);
}

TEST_CASE("directive with leading whitespace is detected", "[compiler][stripper]") {
    auto [out, dirs] = CompilerDirectiveStripper::strip("  `resetall\n", "f.sv");
    REQUIRE(out == "\n");
    REQUIRE(dirs.size() == 1);
    CHECK(dirs[0].kind == DirectiveKind::Resetall);
}

TEST_CASE("backtick token that is not a known directive passes through", "[compiler][stripper]") {
    // `define and `ifdef are pass-2 preprocessor directives — not stripped here
    std::string src = "`define WIDTH 8\n`ifdef SIM\nwire w;\n`endif\n";
    auto [out, dirs] = CompilerDirectiveStripper::strip(src, "f.sv");
    REQUIRE(out == src);
    REQUIRE(dirs.empty());
}
