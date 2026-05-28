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
