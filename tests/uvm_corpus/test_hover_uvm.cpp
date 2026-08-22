#include <catch2/catch_test_macros.hpp>
#include "lsp/hover.h"
#include "uvm_corpus_fixture.h"

// textDocument/hover against the real, full UVM corpus DB. Same regression
// pair as test_definition_uvm.cpp (see its header comment for the
// symbol-table-pollution bug this guards against), checked via hover's
// markdown content instead of an exact Location.

namespace {

lsp::HoverParams makeParams(const std::string& relPath, unsigned line, unsigned col) {
    lsp::HoverParams p;
    p.textDocument.uri   = uriForRelPath(relPath);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

} // namespace

TEST_CASE("hover: uvm_report_object base-class reference describes the real class",
          "[uvm_corpus][hover]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 59 (1-based) -> 0-based 58, "uvm_report_object" at column 36.
    auto result = HoverProvider::getHover(makeParams("base/uvm_component.svh", 58, 36), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());

    REQUIRE(std::holds_alternative<lsp::MarkupContent>(result->contents));
    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("Class") != std::string::npos);
    CHECK(val.find("uvm_report_object") != std::string::npos);
    // The former bug's bogus symbol was a `Signal`, not a `Class` -- this is
    // the sharpest single check that the real class won, not the garbage one.
    CHECK(val.find("Signal") == std::string::npos);
}

TEST_CASE("hover: uvm_transaction base-class reference describes the real class (positive control)",
          "[uvm_corpus][hover]") {
    const std::string text = readCorpusFile("seq/uvm_sequence_item.svh");
    // Line 52 (1-based) -> 0-based 51, "uvm_transaction" at column 32.
    auto result = HoverProvider::getHover(makeParams("seq/uvm_sequence_item.svh", 51, 32), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());

    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("Class") != std::string::npos);
    CHECK(val.find("uvm_transaction") != std::string::npos);
}

TEST_CASE("hover: uvm_component itself resolves with its base class in the detail",
          "[uvm_corpus][hover]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 59 (1-based) -> 0-based 58, "uvm_component" at column 14 (after "virtual class ").
    auto result = HoverProvider::getHover(makeParams("base/uvm_component.svh", 58, 14), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());

    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("Class") != std::string::npos);
    CHECK(val.find("uvm_component") != std::string::npos);
}
