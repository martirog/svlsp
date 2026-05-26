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
