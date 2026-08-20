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

TEST_CASE("function-like macro parameter with default value used when omitted",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define GREET(NAME, GREETING=hello) GREETING NAME\n"
        "`GREET(world)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("hello world") != std::string::npos);
}

TEST_CASE("function-like macro parameter with default value overridden when supplied",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define GREET(NAME, GREETING=hello) GREETING NAME\n"
        "`GREET(world, hi)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("hi world") != std::string::npos);
}

TEST_CASE("function-like macro default value containing nested parens does not hang",
          "[compiler][preprocessor][defaultargs]") {
    // Mirrors real UVM: `define uvm_report_begin(SEVERITY, ID, VERBOSITY,
    // RO=uvm_get_report_object()) -- a default value that is itself a
    // function call, i.e. contains its own '(' and ')'.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B=default_call()) A B\n"
        "`M(x)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("x default_call()") != std::string::npos);
}

TEST_CASE("function-like macro missing required argument without default records error",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(x)\n", "f.sv");
    REQUIRE(!errs.empty());
}

TEST_CASE("function-like macro too many arguments still records error",
          "[compiler][preprocessor][defaultargs]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B=default_val) A B\n"
        "`M(x, y, z)\n", "f.sv");
    REQUIRE(!errs.empty());
}

// ---------------------------------------------------------------------------
// Macro-argument parsing must track {}/[] nesting, and string-literal
// contents, not just ()
// ---------------------------------------------------------------------------

TEST_CASE("macro argument containing a brace expression with a comma is not split",
          "[compiler][preprocessor][bracenesting]") {
    // Mirrors real UVM: `uvm_warning(ID, {"part one ", part_two}) -- the
    // comma inside {...} must not be treated as the argument separator.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"id\", {\"part one \", part_two})\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"id\" {\"part one \", part_two}") != std::string::npos);
}

TEST_CASE("macro argument containing a bracket expression with a comma is not split",
          "[compiler][preprocessor][bracenesting]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"id\", arr[i, j])\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"id\" arr[i, j]") != std::string::npos);
}

TEST_CASE("macro argument string literal containing a comma is not split",
          "[compiler][preprocessor][bracenesting]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"hello, world\", second)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"hello, world\" second") != std::string::npos);
}

TEST_CASE("macro argument string literal containing an unmatched paren is not split",
          "[compiler][preprocessor][bracenesting]") {
    // A quoted message string like "already exists (see above)" must not
    // perturb paren-depth tracking of the invocation itself.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define M(A, B) A B\n"
        "`M(\"already exists (see above)\", second)\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("\"already exists (see above)\" second") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Comments must not be scanned for macro invocations
// ---------------------------------------------------------------------------

TEST_CASE("backtick-like text inside a line comment is not macro-expanded",
          "[compiler][preprocessor][comments]") {
    // Common in doc comments that show example macro usage, e.g. UVM's
    // "// |`uvm_info(ID, MSG, VERBOSITY)" -- must not be treated as an
    // actual invocation of an undefined (or wrong-arity) macro.
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "// example: `uvm_info(ID, MSG, VERBOSITY)\n"
        "wire a;\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("// example: `uvm_info(ID, MSG, VERBOSITY)") != std::string::npos);
}

TEST_CASE("macro invocation before a trailing comment still expands",
          "[compiler][preprocessor][comments]") {
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process(
        "`define WIDTH 8\n"
        "wire [`WIDTH-1:0] bus; // uses `UNDEFINED_MACRO in the comment\n", "f.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire [8-1:0] bus;") != std::string::npos);
    REQUIRE(out.find("// uses `UNDEFINED_MACRO in the comment") != std::string::npos);
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

TEST_CASE("include: __LINE__ inside included file resolves to its own line number",
          "[compiler][preprocessor]") {
    // Pass 1 (CompilerDirectiveStripper) only ever runs on the top-level
    // source before SvPreprocessor::process is called; included files are
    // read as raw text by processInclude. __LINE__ must still resolve
    // against the included file's own line numbers, not be left as a
    // literal, undefined macro invocation.
    std::string tmpPath = "/tmp/svlsp_test_inc_line.sv";
    { std::ofstream f(tmpPath); f << "wire a;\n"
                                     "wire b = `__LINE__;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire b = 2;") != std::string::npos);
}

TEST_CASE("include: __FILE__ inside included file resolves to the included path",
          "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_file.sv";
    { std::ofstream f(tmpPath); f << "string s = `__FILE__;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("string s = \"" + tmpPath + "\";") != std::string::npos);
}

TEST_CASE("include: metadata directive inside included file is stripped without error",
          "[compiler][preprocessor]") {
    // `timescale etc. are pass-1 directives; an included file containing one
    // must not fall through to pass 2 and be treated as an unknown directive.
    std::string tmpPath = "/tmp/svlsp_test_inc_timescale.sv";
    { std::ofstream f(tmpPath); f << "`timescale 1ns/1ps\n"
                                     "wire c;\n"; }

    SvPreprocessor pp;
    auto [out, errs, macros_, map_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire c;") != std::string::npos);
    REQUIRE(out.find("timescale") == std::string::npos);
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
// Multi-line (backslash-continuation) `define bodies
// ---------------------------------------------------------------------------

TEST_CASE("backslash-continuation define merges body across two lines",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDE_WIDTH \\\n"
        "    16\n"
        "wire [`WIDE_WIDTH-1:0] bus;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].name == "WIDE_WIDTH");
    CHECK(result.macros[0].body == "16");
    CHECK(result.macros[0].line == 1);
    CHECK(result.source.find("wire [16-1:0] bus;") != std::string::npos);
}

TEST_CASE("backslash-continuation define merges body across three lines",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define LONG_MACRO a \\\n"
        "b \\\n"
        "c\n"
        "wire w = `LONG_MACRO;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 1);
    CHECK(result.macros[0].body == "a b c");
    CHECK(result.source.find("wire w = a b c;") != std::string::npos);
}

TEST_CASE("backslash-continuation define preserves output line count",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDE_WIDTH \\\n"  // line 1 -> blank
        "    16\n"                // line 2 -> blank
        "\n"                      // line 3 -> blank
        "wire [`WIDE_WIDTH-1:0] bus;\n" // line 4 -> content
        "wire done;\n",           // line 5 -> content
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 5);
    CHECK(result.sourceMap[0].line == 1);
    CHECK(result.sourceMap[1].line == 2);
    CHECK(result.sourceMap[2].line == 3);
    CHECK(result.sourceMap[3].line == 4);
    CHECK(result.sourceMap[4].line == 5);
    int newlines = static_cast<int>(std::count(result.source.begin(), result.source.end(), '\n'));
    CHECK(newlines == 5);
}

TEST_CASE("backslash-continuation define followed by mid-line invocation records correct column shift",
          "[compiler][preprocessor][multiline]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDE_WIDTH \\\n"
        "    16\n"
        "wire [`WIDE_WIDTH-1:0] wide_bus;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    // "`WIDE_WIDTH" (11 chars) expands to "16" (2 chars) -> delta of +9.
    REQUIRE(result.sourceMap[2].colShifts.size() == 1);
    CHECK(result.sourceMap[2].colShifts[0].delta == 9);
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

TEST_CASE("line without macro invocation has no column shifts",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process("wire a;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 1);
    CHECK(result.sourceMap[0].colShifts.empty());
}

TEST_CASE("mid-line shrinking macro invocation records a column shift breakpoint",
          "[compiler][preprocessor][sourcemap]") {
    // "`WIDTH" (6 chars) expands to "8" (1 char) -> delta of +5 for columns
    // at/after the point right after the replacement.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define WIDTH 8\n"
        "wire [`WIDTH-1:0] data_bus;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 2);
    REQUIRE(result.sourceMap[1].colShifts.size() == 1);
    CHECK(result.sourceMap[1].colShifts[0].outputCol == 7);
    CHECK(result.sourceMap[1].colShifts[0].delta == 5);
}

TEST_CASE("mid-line growing macro invocation records a negative column shift",
          "[compiler][preprocessor][sourcemap]") {
    // "`FOO" (4 chars) expands to "abcdefgh" (8 chars) -> delta of -4.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define FOO abcdefgh\n"
        "wire `FOO x;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 2);
    REQUIRE(result.sourceMap[1].colShifts.size() == 1);
    CHECK(result.sourceMap[1].colShifts[0].outputCol == 13);
    CHECK(result.sourceMap[1].colShifts[0].delta == -4);
}

TEST_CASE("multiple macros on one line accumulate column shift delta",
          "[compiler][preprocessor][sourcemap]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define A 1\n"
        "`define B 22\n"
        "wire `A `B x;\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.sourceMap.size() == 3);
    REQUIRE(result.sourceMap[2].colShifts.size() == 2);
    CHECK(result.sourceMap[2].colShifts[0].outputCol == 6);
    CHECK(result.sourceMap[2].colShifts[0].delta == 1);
    CHECK(result.sourceMap[2].colShifts[1].outputCol == 9);
    CHECK(result.sourceMap[2].colShifts[1].delta == 1);
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
