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
// Real-time `include` progress logging (svlsp --build-db's progress counter)
// ---------------------------------------------------------------------------

TEST_CASE("include resolution logs a progress line as soon as the file is opened",
          "[compiler][preprocessor]") {
    // The log line must appear the moment `include` is resolved -- not only
    // once the whole top-level process() call returns -- so a caller like
    // CompilationController's --build-db progress counter gets real
    // incremental feedback even for a single top-level file that `include`s
    // an entire library (the motivating real-world case: one uvm_pkg.sv
    // `include`ing ~140 files, where a post-hoc "log after the whole parse
    // finishes" approach gives no signal at all until the very end).
    std::string tmpPath = "/tmp/svlsp_test_inc_progress_a.sv";
    { std::ofstream f(tmpPath); f << "wire w;\n"; }

    std::ostringstream progress;
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] =
        pp.process("`include \"" + tmpPath + "\"\n", "test.sv", &progress);
    REQUIRE(errs.empty());
    CHECK(progress.str() == "[parsed]   included: " + tmpPath + "\n");
}

TEST_CASE("nested includes each log their own progress line, in encounter order",
          "[compiler][preprocessor]") {
    std::string innerPath = "/tmp/svlsp_test_inc_progress_inner.sv";
    std::string midPath   = "/tmp/svlsp_test_inc_progress_mid.sv";
    { std::ofstream f(innerPath); f << "wire inner;\n"; }
    { std::ofstream f(midPath); f << "`include \"" + innerPath + "\"\nwire mid;\n"; }

    std::ostringstream progress;
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] =
        pp.process("`include \"" + midPath + "\"\n", "test.sv", &progress);
    REQUIRE(errs.empty());
    // mid.sv is opened (and logged) before its own nested `include` of
    // inner.sv is reached, so mid's line comes first.
    CHECK(progress.str() ==
          "[parsed]   included: " + midPath + "\n"
          "[parsed]   included: " + innerPath + "\n");
}

TEST_CASE("a missing include file logs no progress line", "[compiler][preprocessor]") {
    std::ostringstream progress;
    SvPreprocessor pp;
    auto [out, errs, macros_, map_] =
        pp.process("`include \"no_such_file.sv\"\n", "test.sv", &progress);
    REQUIRE(!errs.empty());
    CHECK(progress.str().empty());
}

TEST_CASE("progressLog defaults to null and is a no-op when omitted",
          "[compiler][preprocessor]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_progress_noop.sv";
    { std::ofstream f(tmpPath); f << "wire w;\n"; }

    SvPreprocessor pp;
    // No progressLog argument at all -- must behave exactly like every
    // pre-existing `include` test above, not crash on a null stream.
    auto [out, errs, macros_, map_] = pp.process("`include \"" + tmpPath + "\"\n", "test.sv");
    REQUIRE(errs.empty());
    REQUIRE(out.find("wire w;") != std::string::npos);
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
// Multi-line macro invocations (no backslash — real UVM pattern, e.g.
// `UVM_PH_TRACE(ID, MSG, PH, VERB) with the argument list split across
// physical lines purely via an open '(')
// ---------------------------------------------------------------------------

TEST_CASE("function-like macro invocation with args spanning two lines expands",
          "[compiler][preprocessor][multiinvoke]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B, C) A B C\n"
        "`M(\"x\",\n"
        "y, z)\n", "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.source.find("\"x\" y z") != std::string::npos);
}

TEST_CASE("function-like macro invocation with args spanning three lines expands",
          "[compiler][preprocessor][multiinvoke]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B, C) A B C\n"
        "`M(\"x\",\n"
        "y,\n"
        "z)\n", "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.source.find("\"x\" y z") != std::string::npos);
}

TEST_CASE("function-like macro invocation split across lines mid-statement expands",
          "[compiler][preprocessor][multiinvoke]") {
    // Mirrors real UVM: the invocation doesn't start the line.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B) A B\n"
        "if (x) `M(\"id\",\n"
        "second);\n", "f.sv");
    REQUIRE(result.errors.empty());
    CHECK(result.source.find("if (x) \"id\" second;") != std::string::npos);
}

TEST_CASE("function-like macro invocation spanning lines preserves output line count",
          "[compiler][preprocessor][multiinvoke]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define M(A, B, C) A B C\n"   // line 1 -> blank
        "`M(\"x\",\n"                  // line 2 -> real content
        "y,\n"                         // line 3 -> blank
        "z)\n"                         // line 4 -> blank
        "wire done;\n",                // line 5 -> content
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

TEST_CASE("unknown macro name does not trigger multi-line merging",
          "[compiler][preprocessor][multiinvoke]") {
    // An undefined (or object-like) name followed by an unrelated, unbalanced
    // '(' elsewhere in ordinary code must not cause line-swallowing.
    SvPreprocessor pp;
    auto result = pp.process(
        "wire w = foo(a,\n"
        "b);\n", "f.sv");
    REQUIRE(result.sourceMap.size() == 2);
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

// ---------------------------------------------------------------------------
// Stringification (`"..`") -- Gap C (partial: stringification only, not
// token-pasting)
// ---------------------------------------------------------------------------

TEST_CASE("stringification wraps a substituted macro parameter in quotes",
          "[compiler][preprocessor][stringify]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define STR(T) `\"T`\"\n"
        "string s = `STR(foo);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nstring s = \"foo\";\n");
}

TEST_CASE("stringification reproduces the real UVM `uvm_type_name_decl pattern",
          "[compiler][preprocessor][stringify]") {
    // macros/uvm_object_defines.svh:540 (`uvm_type_name_decl) and :555
    // (`m_uvm_object_registry_internal, which invokes it as
    // `uvm_type_name_decl(`"S`") -- session 3's root-caused Gap C site.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define uvm_type_name_decl(TNAME_STRING) \\\n"
        "  const static function string type_name(); return `\"TNAME_STRING`\"; endfunction\n"
        "`uvm_type_name_decl(uvm_reg_err_service)\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find("return \"uvm_reg_err_service\";") != std::string::npos);
}

TEST_CASE("stringification macro-expands its contents before quoting",
          "[compiler][preprocessor][stringify]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define A hello\n"
        "`define STR() `\"`A`\"\n"
        "string s = `STR();\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\n\nstring s = \"hello\";\n");
}

TEST_CASE("unterminated stringification records an error instead of hanging",
          "[compiler][preprocessor][stringify]") {
    SvPreprocessor pp;
    auto result = pp.process("string s = `\"unterminated;\n", "f.sv");
    REQUIRE_FALSE(result.errors.empty());
}

// ---------------------------------------------------------------------------
// Token-pasting (``)
// ---------------------------------------------------------------------------

TEST_CASE("token-pasting splices two identifier fragments",
          "[compiler][preprocessor][tokenpaste]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define CONCAT(A,B) A``B\n"
        "wire w = `CONCAT(foo,bar);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nwire w = foobar;\n");
}

TEST_CASE("token-pasting splices onto a non-identifier right operand",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's `M__TABLE_Q(QUEUE_NAME) QUEUE_NAME``.value
    // (base/uvm_resource_pool.svh:136) -- the paste is a pure textual
    // splice, not identifier-fragment-only.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define FIELD(NAME) NAME``.value\n"
        "x = `FIELD(rq);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nx = rq.value;\n");
}

TEST_CASE("token-pasting splices three fragments around a parameter",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's base/uvm_packer.svh:449
    // `` function void uvm_packer::get_packed_``T``s (...) ``
    SvPreprocessor pp;
    auto result = pp.process(
        "`define GETTER(T) get_packed_``T``s\n"
        "function void `GETTER(bit) (int x); endfunction\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find("function void get_packed_bits (int x); endfunction") !=
            std::string::npos);
}

TEST_CASE("token-pasting a macro name then invoking it",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's macros/uvm_object_defines.svh:1456
    // `` `uvm_pack_``TYPE``(ARG, __local_packer__) `` -- proves the splice
    // happens before expandStr scans for the invocation's macro name.
    SvPreprocessor pp;
    auto result = pp.process(
        "`define uvm_pack_int(X) pack_int_impl(X)\n"
        "`define DISPATCH(TYPE, ARG) `uvm_pack_``TYPE``(ARG)\n"
        "`DISPATCH(int, myvar);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find("pack_int_impl(myvar);") != std::string::npos);
}

TEST_CASE("token-pasting resolves before a later stringification span sees it",
          "[compiler][preprocessor][tokenpaste]") {
    // Mirrors real UVM's macros/uvm_phase_defines.svh:58
    // `` `uvm_type_name_decl(`"PREFIX``PHASE``_phase`") ``
    SvPreprocessor pp;
    auto result = pp.process(
        "`define STR2(PREFIX, PHASE) `\"PREFIX``PHASE``_phase`\"\n"
        "string s = `STR2(my, build);\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source == "\nstring s = \"mybuild_phase\";\n");
}

TEST_CASE("token-pasting reproduces the real UVM `uvm_register_cb pattern",
          "[compiler][preprocessor][tokenpaste]") {
    // macros/uvm_callback_defines.svh:71-72 -- combines token-pasting and
    // stringification in the same macro body; also the exact site behind
    // session 3's symbol-table-pollution bug (uvm_report_catcher.svh:71).
    SvPreprocessor pp;
    auto result = pp.process(
        "`define uvm_register_cb(T,CB) \\\n"
        "  static local bit m_register_cb_``CB = uvm_callbacks#(T,CB)::m_register_pair(`\"T`\",`\"CB`\");\n"
        "`uvm_register_cb(uvm_report_object,uvm_report_catcher)\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.source.find(
        "static local bit m_register_cb_uvm_report_catcher = "
        "uvm_callbacks#(uvm_report_object,uvm_report_catcher)::m_register_pair("
        "\"uvm_report_object\",\"uvm_report_catcher\");") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Macro records carry parameters, defaults, column and file (plan.md §6.29
// part A)
// ---------------------------------------------------------------------------

TEST_CASE("macro record keeps a function-like macro's parameters and defaults",
          "[compiler][preprocessor][phase6.29]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "`define PLAIN 8\n"
        "  `define M(A, B=1, RO=get_obj(x, y)) A+B\n"
        "`define Z() 0\n", "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 3);

    const auto& plain = result.macros[0];
    CHECK_FALSE(plain.isFunctionLike);
    CHECK(plain.params.empty());
    CHECK(plain.column == 8);
    CHECK(plain.file.empty()); // primary file

    const auto& m = result.macros[1];
    CHECK(m.name == "M");
    CHECK(m.isFunctionLike);
    CHECK(m.line == 2);
    CHECK(m.column == 10);
    REQUIRE(m.params == std::vector<std::string>{"A", "B", "RO"});
    REQUIRE(m.defaults.size() == 3);
    CHECK_FALSE(m.defaults[0].has_value());
    CHECK(m.defaults[1] == std::optional<std::string>{"1"});
    CHECK(m.defaults[2] == std::optional<std::string>{"get_obj(x, y)"});

    const auto& z = result.macros[2];
    CHECK(z.isFunctionLike);
    CHECK(z.params.empty());
}

TEST_CASE("macro record from an included file names that file",
          "[compiler][preprocessor][phase6.29]") {
    std::string tmpPath = "/tmp/svlsp_test_inc_p29.svh";
    { std::ofstream f(tmpPath); f << "// header\n`define INC_M(X) X\n"; }

    SvPreprocessor pp;
    auto result = pp.process("`include \"" + tmpPath + "\"\n`define TOP_M 1\n", "test.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 2);
    CHECK(result.macros[0].name == "INC_M");
    CHECK(result.macros[0].file == tmpPath);
    CHECK(result.macros[0].line == 2);
    CHECK(result.macros[1].name == "TOP_M");
    CHECK(result.macros[1].file.empty());
}
