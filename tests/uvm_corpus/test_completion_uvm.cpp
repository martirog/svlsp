#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "uvm_corpus_fixture.h"
#include <algorithm>

// textDocument/completion against the real, full UVM corpus DB. First cut --
// this feature wasn't part of session 3's already-validated manual query
// set, so coverage here started intentionally lighter than hover/definition/
// documentSymbol/workspaceSymbol: 2 scenarios, both anchored on real,
// well-known top-level UVM classes rather than an exhaustive sweep.
//
// Broadened (handoff.md "Not yet done" #8): the original 2 scenarios only
// ever exercised exact/contiguous-prefix filtering. Neither the empty-prefix
// path (`rows.empty()` early return, unranked full candidate list) nor true
// fuzzy/skip-tolerant scoring (fuzzy_match.h's actual reason for existing)
// had ever been run against real, full-corpus-scale data (~14k+ symbols) --
// only tiny synthetic DBs (test_completion.cpp) or a single small fixture
// file (tests/integration/fixtures/fuzzy_completion.sv). Both gaps are
// concrete (a correctness or perf/ordering issue specific to real scale
// wouldn't show up in either of those), so 2 more scenarios below close
// them; still not an exhaustive sweep.

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

TEST_CASE("completion: empty prefix at real scope returns the full unranked candidate list",
          "[uvm_corpus][completion]") {
    // Same real anchor position as the scenarios above, but with no typed
    // prefix -- exercises findSymbolsVisibleAt's real result set for this
    // one file at real corpus scale (base/uvm_component.svh alone declares
    // 100+ extern methods, each with an out-of-class `uvm_component::name`
    // body definition elsewhere in the same file -- see the note on
    // cross-file visibility below) flowing through completely unfiltered,
    // unlike every other scenario in this file.
    //
    // docText must be synthetic (not the real file text): every real token
    // on line 59 is itself an identifier, so wordAtPosition's "walks left
    // off an id char" behavior (see symbol_utils.h) would always find SOME
    // word there, never a truly empty prefix.
    const std::string docText = std::string(58, '\n') + " ";
    auto result = CompletionProvider::getCompletion(makeParams("base/uvm_component.svh", 58, 0), uvmCorpusDb(), docText);
    REQUIRE_FALSE(result.isNull());
    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();

    // Pinned (plan.md §6.30, "an ancestor's `local` members"): 331 -> 324,
    // the 7 `local` members of uvm_report_object (m_rh_set, m_rh_init) and
    // uvm_object (m_pack, m_unpack_pre, m_unpack_post, m_leaf_name,
    // m_inst_id) are no longer offered in uvm_component. Then 324 -> 325
    // (plan.md §6.32): the `class` keyword, legal in a class body (nested
    // classes).
    CHECK(items.size() == 325);
    CHECK(hasItem(items, "class"));
    CHECK_FALSE(hasItem(items, "m_inst_id"));
    CHECK(hasItem(items, "m_inst_count")); // static protected: still offered
    CHECK(hasItem(items, "uvm_component"));
    // "get_children" is an extern method: prototype at line 117, body
    // `function void uvm_component::get_children(...)` at line 1847, after
    // `endclass`. Since plan.md §6.30 step D both rows are scoped
    // uvm_pkg::uvm_component (the body used to be recorded at uvm_pkg
    // level), and this anchor line is the class header, inside the class's
    // own range -- so this checks the class's members are visible here.
    CHECK(hasItem(items, "get_children"));
    // No typed prefix -> no ranking, so sortText must be left unset.
    for (const auto& item : items) {
        CHECK(item.sortText.value_or("").empty());
    }
}

TEST_CASE("completion: true fuzzy (skip-tolerant, non-prefix) pattern matches a real class name",
          "[uvm_corpus][completion]") {
    // "uvmcmpnt" is a subsequence of "uvm_component" (u-v-m-_-c-o-m-p-o-n-e-n-t,
    // skipping '_','o','o','e','n') but not a prefix of it -- only
    // fuzzyScore's actual skip-tolerant subsequence matching (not
    // exact/contiguous-prefix filtering, which is all the scenarios above
    // exercise) can find it.
    //
    // Deliberately reuses the same-file anchor used by every scenario above
    // rather than a genuinely cross-file target (e.g. "uvm_sequencer" from a
    // different corpus file): findSymbolsVisibleAt's cross-file union only
    // ever includes a symbol whose *own* scope is "" (truly top-level); real
    // UVM classes all live inside `package uvm_pkg`, so their scope is
    // "uvm_pkg", not "" -- invisible cross-file from a plain completion
    // request unless the requesting file itself has an explicit
    // `import uvm_pkg::*` (files inside the package, like this one, don't
    // import themselves). A real cross-file scenario belongs in a fixture
    // that actually has that import, not here.
    const std::string docText = std::string(58, '\n') + "  uvmcmpnt";
    auto result = CompletionProvider::getCompletion(makeParams("base/uvm_component.svh", 58, 10), uvmCorpusDb(), docText);
    REQUIRE_FALSE(result.isNull());
    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "uvm_component"));
}

TEST_CASE("completion: dot completion offers local/protected members only where the cursor's "
          "class may access them",
          "[uvm_corpus][completion][access]") {
    // `this.` inside uvm_component::m_set_full_name's out-of-class body
    // (line 1962): its own local members and uvm_object's protected
    // m_inst_count, but not uvm_object's local m_inst_id.
    {
        const std::string docText = std::string(1961, '\n') + "    this.";
        auto result = CompletionProvider::getCompletion(
            makeParams("base/uvm_component.svh", 1961, 9), uvmCorpusDb(), docText);
        REQUIRE_FALSE(result.isNull());
        auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
        CHECK(hasItem(items, "m_tr_h"));
        CHECK(hasItem(items, "m_inst_count"));
        CHECK(hasItem(items, "get_name"));
        CHECK_FALSE(hasItem(items, "m_inst_id"));
    }
    // `comp.` on uvm_port_base::new's local `uvm_component comp` (line 300):
    // uvm_port_base isn't a uvm_object, so only public members.
    {
        const std::string docText = std::string(299, '\n') + "    comp.";
        auto result = CompletionProvider::getCompletion(
            makeParams("base/uvm_port_base.svh", 299, 9), uvmCorpusDb(), docText);
        REQUIRE_FALSE(result.isNull());
        auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
        CHECK(hasItem(items, "get_name"));
        CHECK(hasItem(items, "get_children"));
        CHECK_FALSE(hasItem(items, "m_tr_h"));
        CHECK_FALSE(hasItem(items, "m_inst_count"));
        CHECK_FALSE(hasItem(items, "m_inst_id"));
    }
}

TEST_CASE("completion: a function argument is a dot receiver, its dimensions included",
          "[uvm_corpus][completion][port]") {
    // uvm_component::get_children(ref uvm_component children[$]) -- the
    // out-of-class body at line 1847; line 1850 is inside it.
    auto itemsAt = [](const std::string& probe) {
        const std::string docText = std::string(1849, '\n') + probe;
        auto result = CompletionProvider::getCompletion(
            makeParams("base/uvm_component.svh", 1849, static_cast<unsigned>(probe.size())),
            uvmCorpusDb(), docText);
        REQUIRE_FALSE(result.isNull());
        return result.get<lsp::Array<lsp::CompletionItem>>();
    };
    const auto queue = itemsAt("    children.");
    CHECK(hasItem(queue, "push_back"));
    CHECK_FALSE(hasItem(queue, "get_name"));
    const auto elem = itemsAt("    children[0].");
    CHECK(hasItem(elem, "get_name"));
    CHECK(hasItem(elem, "m_tr_h")); // uvm_component's own local: cursor is inside it
    CHECK_FALSE(hasItem(elem, "push_back"));
}
