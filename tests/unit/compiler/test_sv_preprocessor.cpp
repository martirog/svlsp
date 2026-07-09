#include <catch2/catch_test_macros.hpp>
#include "compiler/sv_preprocessor.h"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Object-like macros
// ---------------------------------------------------------------------------

TEST_CASE("object-like macro expands", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`define WIDTH 8\nwire [`WIDTH-1:0] bus;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out == "\nwire [8-1:0] bus;\n");
}

TEST_CASE("macro defined with no body expands to empty", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`define SIM\n`ifdef SIM\nwire a;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out == "\n\nwire a;\n\n");
}

TEST_CASE("undef removes macro", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define A 1\n"
        "`undef A\n"
        "`ifdef A\nwire a;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    // block is skipped because A was undefined
    REQUIRE(out.find("wire a;") == std::string::npos);
}

TEST_CASE("undefineall clears all macros", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define A 1\n"
        "`define B 2\n"
        "`undefineall\n"
        "`ifdef A\nwire a;\n`endif\n"
        "`ifdef B\nwire b;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire a;") == std::string::npos);
    REQUIRE(out.find("wire b;") == std::string::npos);
}

TEST_CASE("recursive macro expansion", "[compiler][preprocessor]") {
    // A's body contains a backtick invocation of B, so `A → `B → 42
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define A `B\n"
        "`define B 42\n"
        "wire [`A];\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [42];") != std::string::npos);
}

TEST_CASE("undefined macro expands to empty and records error", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("wire [`UNDEF-1:0] bus;\n", "f.sv");
    REQUIRE(!errs.empty());
    // The undefined token is replaced with empty
    REQUIRE(out.find("`UNDEF") == std::string::npos);
}

TEST_CASE("predefined macro via define() is visible", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("CHIP_TOP", "my_chip");
    auto [out, errs, macros_, map_] = pp.process("wire w = `CHIP_TOP;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("my_chip") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Function-like macros
// ---------------------------------------------------------------------------

TEST_CASE("function-like macro single argument", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define BITS(n) [n-1:0]\n"
        "wire `BITS(8) bus;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [8-1:0] bus;") != std::string::npos);
}

TEST_CASE("function-like macro multiple arguments", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define MAX(a,b) ((a)>(b)?(a):(b))\n"
        "assign x = `MAX(p, q);\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("((p)>(q)?(p):(q))") != std::string::npos);
}

TEST_CASE("function-like macro zero arguments", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define NOP() begin end\n"
        "`NOP()\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("begin end") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Conditional compilation
// ---------------------------------------------------------------------------

TEST_CASE("ifdef defined — block included", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define SIM\n"
        "`ifdef SIM\nwire sim_wire;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_wire;") != std::string::npos);
}

TEST_CASE("ifdef undefined — block skipped", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`ifdef SIM\nwire sim_wire;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_wire;") == std::string::npos);
}

TEST_CASE("ifndef undefined — block included", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`ifndef SYNTHESIS\nwire sim_only;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_only;") != std::string::npos);
}

TEST_CASE("ifndef defined — block skipped", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("SYNTHESIS");
    auto [out, errs, macros_, map_] = pp.process(
        "`ifndef SYNTHESIS\nwire sim_only;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_only;") == std::string::npos);
}

TEST_CASE("else branch taken when ifdef false", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`ifdef SIM\nwire sim_wire;\n`else\nwire synth_wire;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire sim_wire;") == std::string::npos);
    REQUIRE(out.find("wire synth_wire;") != std::string::npos);
}

TEST_CASE("elsif first branch taken", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("A");
    auto [out, errs, macros_, map_] = pp.process(
        "`ifdef A\nwire a;\n`elsif B\nwire b;\n`else\nwire c;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire a;") != std::string::npos);
    REQUIRE(out.find("wire b;") == std::string::npos);
    REQUIRE(out.find("wire c;") == std::string::npos);
}

TEST_CASE("elsif second branch taken", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("B");
    auto [out, errs, macros_, map_] = pp.process(
        "`ifdef A\nwire a;\n`elsif B\nwire b;\n`else\nwire c;\n`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire a;") == std::string::npos);
    REQUIRE(out.find("wire b;") != std::string::npos);
    REQUIRE(out.find("wire c;") == std::string::npos);
}

TEST_CASE("nested ifdef", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    pp.define("OUTER");
    auto [out, errs, macros_, map_] = pp.process(
        "`ifdef OUTER\n"
        "  `ifdef INNER\nwire inner;\n`endif\n"
        "wire outer;\n"
        "`endif\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire inner;") == std::string::npos);
    REQUIRE(out.find("wire outer;") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Include
// ---------------------------------------------------------------------------

TEST_CASE("include inserts file content", "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_a.sv";
    { std::ofstream f(tmpPath); f << "wire from_include;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire from_include;") != std::string::npos);
}

TEST_CASE("include propagates macro definitions", "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_b.sv";
    { std::ofstream f(tmpPath); f << "`define INC_WIDTH 16\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`include \"" + tmpPath + "\"\n"
        "wire [`INC_WIDTH-1:0] bus;\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [16-1:0] bus;") != std::string::npos);
}

TEST_CASE("include via include path", "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_c.sv";
    { std::ofstream f(tmpPath); f << "wire via_path;\n"; }

    SvPreprocessor pp({"/tmp"});
    auto [out, errs, macros_, map_] = pp.process("`include \"svlsp_test_inc_c.sv\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire via_path;") != std::string::npos);
}

TEST_CASE("include missing file records error", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`include \"no_such_file.sv\"\n", "test.sv");
    REQUIRE(!errs.empty());
}

// ---------------------------------------------------------------------------
// Line preservation
// ---------------------------------------------------------------------------

TEST_CASE("directive lines become blank lines preserving line count", "[compiler][preprocessor]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define A 1\n"  // line 1 → blank
        "wire a;\n"      // line 2 → kept
        "`define B 2\n"  // line 3 → blank
        "wire b;\n",     // line 4 → kept
        "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out == "\nwire a;\n\nwire b;\n");
}

// ---------------------------------------------------------------------------
// Macro records (Phase 4.4)
// ---------------------------------------------------------------------------

TEST_CASE("macro record captured for object-like define", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process("`define WIDTH 8\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "WIDTH");
    CHECK(result.macros[0].body == "8");
    CHECK(result.macros[0].line == 1);
}

TEST_CASE("macro record body excludes trailing line comment", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process("`define BUS_W 16 // bus width\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].body == "16");
}

TEST_CASE("multiple macro records captured in order", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define FOO 1\n"
        "wire a;\n"
        "`define BAR 2\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 2);
    CHECK(result.macros[0].name == "FOO");
    CHECK(result.macros[0].line == 1);
    CHECK(result.macros[1].name == "BAR");
    CHECK(result.macros[1].line == 3);
}

TEST_CASE("macro in inactive ifdef branch not captured", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`ifdef NEVER_DEFINED\n"
        "`define HIDDEN 42\n"
        "`endif\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.macros.empty());
}

TEST_CASE("macro in active ifdef branch captured", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    pp.define("SIM");
    auto result = pp.process(
        "`ifdef SIM\n"
        "`define CLK_PERIOD 10\n"
        "`endif\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "CLK_PERIOD");
}

TEST_CASE("function-like macro record captured", "[compiler][preprocessor][phase44]") {
    SvPreprocessor pp;
    auto result = pp.process("`define MAX(a, b) ((a) > (b) ? (a) : (b))\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "MAX");
    CHECK(!result.macros[0].body.empty());
}

// ---------------------------------------------------------------------------
// Source map
// ---------------------------------------------------------------------------

TEST_CASE("source map has one entry per output line for plain source",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "wire a;\n"
        "wire b;\n"
        "wire c;\n", "main.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    // Primary file lines use "" (caller already knows the file path).
    CHECK(result.sourceMap[0].file == "");
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].file == "");
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].file == "");
    CHECK(result.sourceMap[2].line == 3);
}

TEST_CASE("source map directive lines map to their original line",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define A 1\n"   // line 1 → blank output line
        "wire a;\n"       // line 2
        "`define B 2\n",  // line 3 → blank output line
        "main.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    CHECK(result.sourceMap[0].file == "");
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].file == "");
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].file == "");
    CHECK(result.sourceMap[2].line == 3);
}

TEST_CASE("source map include lines point to included file",
          "[compiler][preprocessor][sourcemap]") {
    std::string tmpPath = "/tmp/svlsp_test_sm_inc.sv";
    { std::ofstream f(tmpPath); f << "wire x;\n"; }

    SvPreprocessor pp;
    auto result = pp.process(
        "wire a;\n"
        "`include \"" + tmpPath + "\"\n"
        "wire b;\n",
        "main.sv");
    REQUIRE(result.errors.empty());
    // 3 output lines: wire a (primary:""), wire x (inc:tmpPath), wire b (primary:"")
    REQUIRE(result.sourceMap.size() == 3);
    CHECK(result.sourceMap[0].file == "");      // primary file
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].file == tmpPath); // included file
    CHECK(result.sourceMap[1].line == 1);
    CHECK(result.sourceMap[2].file == "");      // primary file
    CHECK(result.sourceMap[2].line == 3);
}

TEST_CASE("source map entry count equals output line count",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define W 8\n"
        "`ifdef SIM\n"
        "wire sim_only;\n"
        "`endif\n"
        "wire w;\n",
        "f.sv");
    // Count '\n' in output
    int newlines = static_cast<int>(std::count(result.source.begin(), result.source.end(), '\n'));
    CHECK(static_cast<int>(result.sourceMap.size()) == newlines);
}
