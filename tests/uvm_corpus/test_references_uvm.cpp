#include <catch2/catch_test_macros.hpp>
#include "lsp/references.h"
#include "uvm_corpus_fixture.h"
#include <chrono>
#include <filesystem>
#include <set>
#include <tuple>

// textDocument/references against the real, full UVM corpus DB
// (plan.md §6.30 step B). Every expected set below was checked by hand
// against `grep -rnw <name>` over the corpus (comment-only mentions
// excluded): each name is also declared by an unrelated class, whose uses a
// lexical search wrongly reported.

namespace {

using Loc = std::tuple<std::string, unsigned, unsigned>; // uri path, 0-based line, col

std::optional<std::string> corpusText(const std::string& path)
{
    try {
        // DB paths are corpus-relative for `include`d files; an absolute
        // path overrides the root in operator/.
        return readCorpusFile(path);
    } catch (...) {
        return std::nullopt;
    }
}

std::set<Loc> refsAt(const std::string& relPath, unsigned line, unsigned col)
{
    lsp::ReferenceParams p;
    p.textDocument.uri           = uriForRelPath(relPath);
    p.position.line              = line;
    p.position.character         = col;
    p.context.includeDeclaration = true;
    auto result = ReferencesProvider::getReferences(p, uvmCorpusDb(), readCorpusFile(relPath),
                                                    corpusText);
    std::set<Loc> out;
    if (result.isNull()) return out;
    for (const auto& loc : result.value())
        out.insert({std::string(loc.uri.path()), loc.range.start.line, loc.range.start.character});
    return out;
}

Loc at(const std::string& relPath, unsigned line, unsigned col)
{
    return {expectedUriPath(relPath), line, col};
}

} // namespace

TEST_CASE("references: uvm_barrier::cancel excludes uvm_event_base::cancel and its call",
          "[uvm_corpus][references]") {
    // base/uvm_barrier.svh:201 (1-based) "  virtual function void cancel ();"
    // base/uvm_barrier.svh:202           "    m_event.cancel();"  -- m_event is a
    //   uvm_event#(uvm_object), so this calls uvm_event_base::cancel.
    // base/uvm_event.svh:233             "    virtual function void cancel ();"
    CHECK(refsAt("base/uvm_barrier.svh", 200, 24) ==
          std::set<Loc>{at("base/uvm_barrier.svh", 200, 24)});
}

TEST_CASE("references: uvm_event_base::cancel includes the call through a uvm_event#(T) field",
          "[uvm_corpus][references]") {
    CHECK(refsAt("base/uvm_event.svh", 232, 26) ==
          std::set<Loc>{at("base/uvm_event.svh", 232, 26), at("base/uvm_barrier.svh", 201, 12)});
}

TEST_CASE("references: uvm_comparer::set_threshold -- prototype and out-of-class body, not "
          "uvm_barrier's",
          "[uvm_corpus][references]") {
    // base/uvm_comparer.svh:102 "  extern virtual function void set_threshold (...);"
    // base/uvm_comparer.svh:699 "function void uvm_comparer::set_threshold (...);"
    // base/uvm_barrier.svh:165  "  virtual function void set_threshold (int threshold);"
    CHECK(refsAt("base/uvm_comparer.svh", 101, 31) ==
          std::set<Loc>{at("base/uvm_comparer.svh", 101, 31),
                        at("base/uvm_comparer.svh", 698, 28)});
}

TEST_CASE("references: timing for a name used throughout the corpus",
          "[uvm_corpus][references][timing]") {
    // `get_name` is declared by uvm_object and called all over the corpus:
    // every lexical hit is resolved, so this is the worst realistic case
    // (plan.md §6.30 "Performance"). Records the time; the bound only
    // catches a pathological regression.
    const auto t0 = std::chrono::steady_clock::now();
    lsp::ReferenceParams p;
    p.textDocument.uri           = uriForRelPath("base/uvm_object.svh");
    const std::string text       = readCorpusFile("base/uvm_object.svh");
    // First line declaring get_name.
    unsigned line = 0;
    size_t col = std::string::npos, start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string l = text.substr(start, end - start);
        if (l.find("function string get_name") != std::string::npos) {
            col = l.find("get_name");
            break;
        }
        start = end + 1;
        ++line;
    }
    REQUIRE(col != std::string::npos);
    p.position.line              = line;
    p.position.character         = static_cast<unsigned>(col);
    p.context.includeDeclaration = true;
    auto result = ReferencesProvider::getReferences(p, uvmCorpusDb(), text, corpusText);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    REQUIRE_FALSE(result.isNull());
    WARN("references(get_name): " << result.value().size() << " locations in " << ms << " ms");
    CHECK(ms < 30000);
}
