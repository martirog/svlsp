#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "uvm_corpus_fixture.h"
#include <algorithm>

// textDocument/completion against the real, full UVM corpus DB. First cut --
// this feature wasn't part of session 3's already-validated manual query
// set, so coverage here is intentionally lighter than hover/definition/
// documentSymbol/workspaceSymbol: 2 scenarios, both anchored on real,
// well-known top-level UVM classes rather than an exhaustive sweep.

namespace {

lsp::CompletionParams makeParams(const std::string& relPath, unsigned line, unsigned col) {
    lsp::CompletionParams p;
    p.textDocument.uri   = uriForRelPath(relPath);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

bool hasItem(const lsp::Array<lsp::CompletionItem>& items, std::string_view name) {
    return std::any_of(items.begin(), items.end(),
                       [&](const auto& i){ return i.label == name; });
}

} // namespace

TEST_CASE("completion: cursor on a fully-typed real class name includes that class",
          "[uvm_corpus][completion]") {
    const std::string text = readCorpusFile("base/uvm_component.svh");
    // Line 59 (1-based) -> 0-based 58; end of "uvm_component" is column 27
    // (starts at 14, 13 characters long).
    auto result = CompletionProvider::getCompletion(makeParams("base/uvm_component.svh", 58, 27), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "uvm_component"));
}

TEST_CASE("completion: a real known file + a synthetic partial prefix filters to matching real symbols",
          "[uvm_corpus][completion]") {
    // path/line are a real, DB-known position (so findSymbolsVisibleAt has a
    // real scope chain to work from); docText is a synthetic string
    // simulating the user mid-typing "uvm_comp" -- CompletionProvider
    // doesn't cross-validate docText against what's actually in the DB for
    // that path (matching how tests/unit/lsp/test_completion.cpp's own
    // existing cases work), only uses it to extract the typed prefix.
    // wordAtPosition indexes into docText by (line, col), so docText needs
    // 58 lines before the one actually being tested to line up with the
    // real line=58 used for the scope lookup below.
    const std::string text = std::string(58, '\n') + "  uvm_comp";
    auto result = CompletionProvider::getCompletion(makeParams("base/uvm_component.svh", 58, 10), uvmCorpusDb(), text);
    REQUIRE_FALSE(result.isNull());
    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "uvm_component"));
    // Every returned item must actually start with the typed prefix.
    for (const auto& item : items) {
        CHECK(std::string(item.label).rfind("uvm_comp", 0) == 0);
    }
}
