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

// ---------------------------------------------------------------------------
// Doc comments (plan.md §6.31)
// ---------------------------------------------------------------------------

TEST_CASE("hover: an extern prototype with only a tag above shows its body's doc",
          "[uvm_corpus][hover][phase6.31]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 666 (1-based) -> 0-based 665: "  extern virtual function void build();"
    // under "// @uvm-compat ..." only; the out-of-class body is documented
    // "// contains default behavior for build_phase()".
    auto result = HoverProvider::getHover(makeParams("base/uvm_component.svh", 665, 32),
                                          uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("**Function** `build`") == 0);
    CHECK(val.find("contains default behavior for build_phase()") != std::string::npos);
    CHECK(val.find("@uvm") == std::string::npos);
}

TEST_CASE("hover: a method whose doc sits one blank line above a tag shows none",
          "[uvm_corpus][hover][phase6.31]") {
    const std::string text = readCorpusFile("base/uvm_object.svh");
    // Line 118 (1-based) -> 0-based 117: "  extern virtual function string get_name ();"
    // -- the real doc is above a blank line and a `@uvm-ieee` tag, and the
    // body's block above is a `// get_name` / `// ---` banner then a blank.
    auto result = HoverProvider::getHover(makeParams("base/uvm_object.svh", 117, 35),
                                          uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("**Function** `get_name`") == 0);
    CHECK(val.find("Returns the name") == std::string::npos);
    CHECK(val.find("@uvm") == std::string::npos);
}

TEST_CASE("doc comments: count recorded across the UVM corpus",
          "[uvm_corpus][phase6.31]") {
    auto& db = uvmCorpusDb();
    int symbols = 0, documented = 0;
    for (const auto& path : db.allFilePaths())
        for (const auto& row : db.symbolsForFile(path)) {
            ++symbols;
            if (!db.docFor(row).empty()) ++documented;
        }
    // Measured 2026-09-28 (the same 960 rows `--build-db` stores in
    // symbol_docs); a change here means the extraction rules changed.
    CHECK(symbols == 15731);
    CHECK(documented == 960);
}
