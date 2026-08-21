#include <catch2/catch_test_macros.hpp>
#include <antlr4-runtime.h>
#include "SvLexer.h"
#include "SvParser.h"
#include <fstream>
#include <sstream>

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    REQUIRE(f.is_open());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int parseErrors(const std::string& src) {
    antlr4::ANTLRInputStream input(src);
    SvLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SvParser parser(&tokens);
    lexer.removeErrorListeners();
    parser.removeErrorListeners();
    parser.source_text();
    return static_cast<int>(parser.getNumberOfSyntaxErrors());
}

TEST_CASE("SvParser parses module_basic.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/module_basic.sv")) == 0);
}

TEST_CASE("SvParser parses module_params.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/module_params.sv")) == 0);
}

TEST_CASE("SvParser parse tree root is non-null for valid input", "[compiler][parser]") {
    std::string src = "module top; endmodule\n";
    antlr4::ANTLRInputStream input(src);
    SvLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SvParser parser(&tokens);
    lexer.removeErrorListeners();
    parser.removeErrorListeners();

    antlr4::tree::ParseTree* tree = parser.source_text();
    REQUIRE(tree != nullptr);
    REQUIRE(parser.getNumberOfSyntaxErrors() == 0);
}

TEST_CASE("SvParser reports errors for malformed input", "[compiler][parser]") {
    // Brace is not valid SV module syntax
    REQUIRE(parseErrors("module bad { endmodule\n") > 0);
}

TEST_CASE("SvParser parses interfaces.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/interfaces.sv")) == 0);
}

TEST_CASE("SvParser parses always_blocks.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/always_blocks.sv")) == 0);
}

TEST_CASE("SvParser parses functions_tasks.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/functions_tasks.sv")) == 0);
}

TEST_CASE("SvParser parses classes.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/classes.sv")) == 0);
}

TEST_CASE("SvParser parses packages.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/packages.sv")) == 0);
}

TEST_CASE("SvParser parses structs_unions.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/structs_unions.sv")) == 0);
}

TEST_CASE("SvParser parses enums.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/enums.sv")) == 0);
}

TEST_CASE("SvParser parses generate.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/generate.sv")) == 0);
}

TEST_CASE("SvParser parses assertions.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/assertions.sv")) == 0);
}

TEST_CASE("SvParser parses clocking.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/clocking.sv")) == 0);
}

TEST_CASE("SvParser parses coverage.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/coverage.sv")) == 0);
}

TEST_CASE("SvParser parses constraints.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/constraints.sv")) == 0);
}

TEST_CASE("SvParser parses macros.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/macros.sv")) == 0);
}

TEST_CASE("SvParser parses timescale.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/timescale.sv")) == 0);
}

TEST_CASE("SvParser parses bind.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/bind.sv")) == 0);
}

TEST_CASE("SvParser parses program.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/program.sv")) == 0);
}

TEST_CASE("SvParser parses checker.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/checker.sv")) == 0);
}

TEST_CASE("SvParser parses dpi.sv with no errors", "[compiler][parser]") {
    REQUIRE(parseErrors(readFile(SV_EXAMPLES_DIR "/dpi.sv")) == 0);
}

TEST_CASE("SvParser parses a string literal containing an escaped quote", "[compiler][parser][stringescape]") {
    // Regression test for the STRING_LITERAL lexer rule not handling `\"`
    // (found via the real UVM corpus: `reg/uvm_vreg.svh:435`,
    // `` `uvm_error("RegModel", $sformatf("Virtual register \"%s\" cannot
    // have 0 bits", name)) `` -- pattern reproduced here without the macro
    // layer, since STRING_LITERAL is a pure lexer rule).
    std::string src =
        "module top;\n"
        "  initial $display(\"Virtual register \\\"%s\\\" cannot have 0 bits\", \"x\");\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser parses a string literal containing an escaped backslash", "[compiler][parser][stringescape]") {
    std::string src =
        "module top;\n"
        "  initial $display(\"a\\\\b\");\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser does not let an escaped quote swallow the rest of the line", "[compiler][parser][stringescape]") {
    // Mirrors the real cascade pattern from `base/uvm_root.svh:600`: a
    // concatenation expression with plain (non-escaped) string arguments
    // following one that itself contains an escaped quote -- if the lexer
    // mis-terminates the first string early, everything after it
    // mis-tokenizes and the whole statement fails to parse.
    std::string src =
        "module top;\n"
        "  string test_name;\n"
        "  initial $display({\"before \\\"quoted\\\" after \", test_name, \"...\"});\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still reports an error for a truly unterminated string", "[compiler][parser][stringescape]") {
    // Guards against a too-permissive fix (e.g. accidentally consuming to EOF).
    REQUIRE(parseErrors("module top; initial $display(\"unterminated); endmodule\n") > 0);
}

TEST_CASE("SvParser parses the LRM-correct void'(...) cast form", "[compiler][parser][voidcast]") {
    // Regression test for subroutine_call_statement previously requiring
    // 'void' '(' ... ')' ';' with no SINGLE_QUOTE -- real UVM uses the
    // LRM-correct `void'(...)` form pervasively (e.g. base/uvm_root.svh:916,
    // `void'($sscanf(timeout,"%d,%s",timeout_int,override_spec));`).
    std::string src =
        "module top;\n"
        "  function int f(); return 0; endfunction\n"
        "  initial void'(f());\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}

TEST_CASE("SvParser still parses the void(...) workaround form", "[compiler][parser][voidcast]") {
    // Guards against the SINGLE_QUOTE? fix accidentally making it required.
    std::string src =
        "module top;\n"
        "  function int f(); return 0; endfunction\n"
        "  initial void(f());\n"
        "endmodule\n";
    REQUIRE(parseErrors(src) == 0);
}
