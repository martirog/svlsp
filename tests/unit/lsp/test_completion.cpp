#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include "db/compilation_controller.h"
#include <algorithm>
#include <map>
#include <filesystem>
#include <fstream>

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::CompletionParams makeParams(std::string_view path, unsigned line, unsigned col) {
    lsp::CompletionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

bool hasItem(const lsp::Array<lsp::CompletionItem>& items, std::string_view name) {
    return std::any_of(items.begin(), items.end(),
                       [&](const auto& i){ return i.label == name; });
}

} // namespace

TEST_CASE("CompletionProvider: empty DB still offers top-level keywords", "[completion][keyword]")
{
    // Empty DB, cursor outside any tracked scope, so scopeKindAtPosition is
    // "" (top level) -- keyword completion (plan.md §6.9) should still
    // surface top-level-legal keywords like "module" even with zero DB
    // symbols, fixing §6.9's flagged rows.empty() early-return gap.
    Fixture f;
    const std::string text = "module top;\n  \nendmodule";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 2), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "module"));
    CHECK(hasItem(items, "class"));
}

TEST_CASE("CompletionProvider: returns top-level symbols from all files", "[completion]")
{
    Fixture f;
    // Two separate files, both define modules at the top level (scope = "")
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a.sv", "h"),
        {{ParseRecordKind::Module, "adder",   1, 7, "", "", 10, ""}});
    f.sdb.replaceSymbols(f.sdb.upsertFile("/b.sv", "h"),
        {{ParseRecordKind::Module, "arbiter", 1, 7, "", "", 20, ""}});

    // Cursor at line 0 char 0 of /top.sv (not in DB — empty scope)
    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/top.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "adder"));
    CHECK(hasItem(items, "arbiter"));
}

TEST_CASE("CompletionProvider: filters by typed prefix", "[completion]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,    "adder",   1, 7, "", "", 10, ""},
        {ParseRecordKind::Parameter, "WIDTH",   5, 4, "adder", "", 0, "adder"},
        {ParseRecordKind::Port,      "clk",     7, 4, "adder", "input", 0, "adder"},
    });

    // Cursor is positioned right after the partial token "W" on line 4 (inside adder scope)
    // We're looking for completions starting with "W"
    // Use line=4 (0-based, maps to 1-based 5 which is inside adder's scope)
    const std::string text = "module adder #(\n    parameter int WIDTH = 8\n) (\n    input clk\n    W";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 4, 5), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "WIDTH"));
    CHECK_FALSE(hasItem(items, "clk"));   // doesn't start with "W"
    CHECK_FALSE(hasItem(items, "adder")); // doesn't start with "W"
}

TEST_CASE("CompletionProvider: completion items have correct kind", "[completion]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",  1, 7, "", "", 20, ""},
        {ParseRecordKind::Function, "calc", 3, 9, "top", "logic", 8, "top"},
    });

    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    for (const auto& item : items) {
        if (item.label == "top")
            CHECK(static_cast<lsp::CompletionItemKind>(item.kind.value()) == lsp::CompletionItemKind::Module);
        if (item.label == "calc")
            CHECK(static_cast<lsp::CompletionItemKind>(item.kind.value()) == lsp::CompletionItemKind::Function);
    }
}

TEST_CASE("CompletionProvider: fuzzy-matches a non-contiguous/typo'd prefix", "[completion]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,    "adder",   1, 7, "", "", 10, ""},
        {ParseRecordKind::Parameter, "WIDTH",   5, 4, "adder", "", 0, "adder"},
        {ParseRecordKind::Port,      "clk",     7, 4, "adder", "input", 0, "adder"},
    });

    // "wdth" skips the 'I' in WIDTH — not a strict prefix, but still a valid
    // (typo-tolerant) subsequence match.
    const std::string text =
        "module adder #(\n    parameter int WIDTH = 8\n) (\n    input clk\n    wdth";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 4, 8), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "WIDTH"));
    CHECK_FALSE(hasItem(items, "clk"));   // "wdth" is not a subsequence of "clk"
    CHECK_FALSE(hasItem(items, "adder")); // "wdth" is not a subsequence of "adder"
}

TEST_CASE("CompletionProvider: ranks a contiguous-prefix match above a scattered match", "[completion]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "report_id", 1, 7, "", "", 5, ""},
        {ParseRecordKind::Module, "xxrepxx",   2, 7, "", "", 5, ""},
    });

    // "rep" is a contiguous, word-start prefix of report_id, but only a
    // mid-word match inside xxrepxx — report_id should be ranked first.
    const std::string text = "rep";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 0, 3), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    // Since keyword completion (plan.md §6.9) merges in any "rep"-matching
    // legal keywords too (e.g. "repeat"), assert relative order rather than
    // an exact 2-item list.
    auto posOf = [&](std::string_view name) {
        return std::find_if(items.begin(), items.end(),
                            [&](const auto& i){ return i.label == name; });
    };
    auto itReport = posOf("report_id");
    auto itXxrep  = posOf("xxrepxx");
    REQUIRE(itReport != items.end());
    REQUIRE(itXxrep != items.end());
    CHECK(std::distance(items.begin(), itReport) < std::distance(items.begin(), itXxrep));
    // Ranking must also be encoded in sortText, for clients that re-sort by it.
    REQUIRE(itReport->sortText.has_value());
    REQUIRE(itXxrep->sortText.has_value());
    CHECK(*itReport->sortText < *itXxrep->sortText);
}

// ---------------------------------------------------------------------------
// Configurable fuzzy-matching toggle (plan.md §6.11)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: fuzzyEnabled=false rejects a non-contiguous/typo'd prefix",
          "[completion][fuzzy-toggle]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,    "adder",   1, 7, "", "", 10, ""},
        {ParseRecordKind::Parameter, "WIDTH",   5, 4, "adder", "", 0, "adder"},
    });

    // Same "wdth" skip-tolerant prefix the fuzzy-matching test above proves
    // matches WIDTH -- with fuzzy matching off, this must fail strict-prefix
    // and drop out entirely (not just rank lower).
    const std::string text =
        "module adder #(\n    parameter int WIDTH = 8\n) (\n    wdth";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 3, 8), f.sdb, text,
                                                     /*fuzzyEnabled=*/false);
    CHECK(result.isNull());
}

TEST_CASE("CompletionProvider: fuzzyEnabled=false still matches a strict prefix",
          "[completion][fuzzy-toggle]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,    "adder",   1, 7, "", "", 10, ""},
        {ParseRecordKind::Parameter, "WIDTH",   5, 4, "adder", "", 0, "adder"},
        {ParseRecordKind::Port,      "clk",     7, 4, "adder", "input", 0, "adder"},
    });

    const std::string text = "module adder #(\n    parameter int WIDTH = 8\n) (\n    input clk\n    W";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 4, 5), f.sdb, text,
                                                     /*fuzzyEnabled=*/false);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "WIDTH"));
    CHECK_FALSE(hasItem(items, "clk"));
    CHECK_FALSE(hasItem(items, "adder"));
    // No ranking applied when fuzzy matching is off.
    for (auto& item : items)
        CHECK_FALSE(item.sortText.has_value());
}

TEST_CASE("CompletionProvider: fuzzyEnabled=false preserves DB order, not fuzzy tie-break order",
          "[completion][fuzzy-toggle]")
{
    Fixture f;
    // findSymbolsVisibleAt's own DB order sorts by scope depth first (a
    // deeper-scoped match wins ties over a top-level one), only falling
    // back to name within equal scope depth -- so "abZ" (scoped inside
    // "adder") sorts *before* "abA" (top-level), even though "abA" < "abZ"
    // alphabetically. Both fuzzy-score identically against prefix "ab" (an
    // identical two-char contiguous word-start match on each), so with
    // fuzzy matching *enabled* the tie is broken purely by name -- "abA"
    // first, reversed from DB order. Disabling fuzzy matching must keep DB
    // order, not fall back to this alphabetical tie-break.
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,    "adder", 1, 7, "", "", 20, ""},
        {ParseRecordKind::Module,    "abA",   2, 7, "", "", 2, ""},
        {ParseRecordKind::Parameter, "abZ",   6, 4, "adder", "", 0, "adder"},
    });

    const std::string text = "module abA;\nmodule adder;\n\n\n\n    ab";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 5, 6), f.sdb, text,
                                                     /*fuzzyEnabled=*/false);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    auto posOf = [&](std::string_view name) {
        return std::find_if(items.begin(), items.end(),
                            [&](const auto& i){ return i.label == name; });
    };
    auto itAbZ = posOf("abZ");
    auto itAbA = posOf("abA");
    REQUIRE(itAbZ != items.end());
    REQUIRE(itAbA != items.end());
    CHECK(std::distance(items.begin(), itAbZ) < std::distance(items.begin(), itAbA));
}

TEST_CASE("CompletionProvider: fuzzyEnabled=true (default) is unchanged from omitting the flag",
          "[completion][fuzzy-toggle]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,    "adder", 1, 7, "", "", 10, ""},
        {ParseRecordKind::Parameter, "WIDTH", 5, 4, "adder", "", 0, "adder"},
    });

    // Same "wdth" skip-tolerant prefix/position the fuzzy-matching test
    // above proves matches WIDTH.
    const std::string text =
        "module adder #(\n    parameter int WIDTH = 8\n) (\n    input clk\n    wdth";
    auto withDefault = CompletionProvider::getCompletion(makeParams("/t.sv", 4, 8), f.sdb, text);
    auto withExplicitTrue = CompletionProvider::getCompletion(makeParams("/t.sv", 4, 8), f.sdb,
                                                               text, /*fuzzyEnabled=*/true);
    REQUIRE_FALSE(withDefault.isNull());
    REQUIRE_FALSE(withExplicitTrue.isNull());
    CHECK(hasItem(withDefault.get<lsp::Array<lsp::CompletionItem>>(), "WIDTH"));
    CHECK(hasItem(withExplicitTrue.get<lsp::Array<lsp::CompletionItem>>(), "WIDTH"));
}

// ---------------------------------------------------------------------------
// Dot / member-access completion (plan.md §6.10)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: dot-completion narrows to the object's class members", "[completion][dot]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",        10, ""},
        {ParseRecordKind::Signal,   "foo",     2, 2, "top",     "MyClass", 0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 5, 7,  "",        "",        8,  ""},
        {ParseRecordKind::Function, "bar",     6, 10, "MyClass", "void",    6,  "MyClass"},
        {ParseRecordKind::Signal,   "baz",     7, 10, "MyClass", "",        0,  "MyClass"},
    });

    // "  foo." (0-based line 1), cursor right after the dot.
    const std::string text = "module top;\n  foo.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "bar"));
    CHECK(hasItem(items, "baz"));
    // Only MyClass's own members -- not foo itself or unrelated top-level symbols.
    CHECK_FALSE(hasItem(items, "foo"));
    CHECK_FALSE(hasItem(items, "top"));
    CHECK_FALSE(hasItem(items, "MyClass"));
}

TEST_CASE("CompletionProvider: dot-completion offers a prototype-only (endLine==0) method",
          "[completion][dot]")
{
    // Confirms the completion pipeline needs no changes of its own for
    // plan.md §6.16 (pure virtual/extern/interface-class method
    // prototypes, src/compiler/sv_tree_walker.cpp) -- a Function row with
    // no body (endLine == 0, same leaf-symbol convention Port/Signal/
    // Parameter/Macro already use) flows through exactly like any other.
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",       1, 7,  "",        "",        10, ""},
        {ParseRecordKind::Signal,   "foo",       2, 2, "top",     "MyClass", 0,  "top"},
        {ParseRecordKind::Class,    "MyClass",   5, 7,  "",        "",        8,  ""},
        {ParseRecordKind::Function, "get_val",   6, 10, "MyClass", "int",     0,  "MyClass"},
    });

    const std::string text = "module top;\n  foo.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "get_val"));
}

TEST_CASE("CompletionProvider: dot-completion filters class members by typed prefix", "[completion][dot]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",        10, ""},
        {ParseRecordKind::Signal,   "foo",     2, 2, "top",     "MyClass", 0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 5, 7,  "",        "",        8,  ""},
        {ParseRecordKind::Function, "alpha",   6, 10, "MyClass", "void",    6,  "MyClass"},
        {ParseRecordKind::Function, "beta",    7, 10, "MyClass", "void",    6,  "MyClass"},
    });

    // "  foo.al" (0-based line 1), cursor right after "al".
    const std::string text = "module top;\n  foo.al";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 8), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "alpha"));
    CHECK_FALSE(hasItem(items, "beta"));
}

TEST_CASE("CompletionProvider: the prefix is only the text before the cursor",
          "[completion][prefix]")
{
    // plan.md §6.33 F6/C7: the rest of the word after the cursor must not
    // filter -- completing at the start of `alpha` offers `beta` too.
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",        7,  ""},
        {ParseRecordKind::Signal,   "alpha",   2, 6, "top",     "int",     0,  "top"},
        {ParseRecordKind::Signal,   "beta",    3, 6, "top",     "int",     0,  "top"},
        {ParseRecordKind::Signal,   "foo",     4, 10, "top",    "MyClass", 0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 8, 6,  "",        "",        11, ""},
        {ParseRecordKind::Function, "alpha_m", 9, 16, "MyClass", "void",    9,  "MyClass"},
        {ParseRecordKind::Function, "beta_m",  10, 16, "MyClass", "void",   10, "MyClass"},
    });
    const std::string text =
        "module top;\n  int alpha;\n  int beta;\n  MyClass foo;\n  foo.alpha_m;\n  alpha = 1;\n"
        "endmodule\nclass MyClass;\n  function void alpha_m(); endfunction\n"
        "  function void beta_m(); endfunction\nendclass\n";
    auto items = [&](unsigned line, unsigned col) {
        auto r = CompletionProvider::getCompletion(makeParams("/t.sv", line, col), f.sdb, text);
        REQUIRE_FALSE(r.isNull());
        return r.get<lsp::Array<lsp::CompletionItem>>();
    };

    SECTION("bare name, cursor at the start of a word") {
        auto it = items(5, 2);
        CHECK(hasItem(it, "alpha"));
        CHECK(hasItem(it, "beta"));
    }
    SECTION("bare name, cursor in the middle of a word") {
        auto it = items(5, 4); // al|pha
        CHECK(hasItem(it, "alpha"));
        CHECK_FALSE(hasItem(it, "beta"));
    }
    SECTION("dot, cursor right after the dot") {
        auto it = items(4, 6); // foo.|alpha_m
        CHECK(hasItem(it, "alpha_m"));
        CHECK(hasItem(it, "beta_m"));
    }
    SECTION("dot, cursor in the middle of the member") {
        auto it = items(4, 8); // foo.al|pha_m
        CHECK(hasItem(it, "alpha_m"));
        CHECK_FALSE(hasItem(it, "beta_m"));
    }
}

TEST_CASE("CompletionProvider: dot-completion on a built-in-typed variable returns no completions", "[completion][dot]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "", 10, ""},
        {ParseRecordKind::Signal, "x",   2, 10, "top", "", 0,  "top"}, // built-in type -> no detail
    });

    // "  x." (0-based line 1), cursor right after the dot.
    const std::string text = "module top;\n  x.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 4), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("CompletionProvider: dot-completion on an undeclared object returns no completions", "[completion][dot]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7, "", "", 10, ""},
    });

    // "  bogus." (0-based line 1), cursor right after the dot.
    const std::string text = "module top;\n  bogus.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 8), f.sdb, text);
    REQUIRE(result.isNull());
}

// ---------------------------------------------------------------------------
// Built-in container/type method completion (plan.md §6.13)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: dot-completion on a queue offers queue methods only",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",              10, ""},
        {ParseRecordKind::Signal, "q",   2, 2, "top", CONTAINER_QUEUE, 0,  "top"},
    });

    const std::string text = "module top;\n  q.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 4), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "push_back"));
    CHECK(hasItem(items, "pop_front"));
    CHECK_FALSE(hasItem(items, "exists")); // associative-array-only
}

TEST_CASE("CompletionProvider: dot-completion on an associative array offers its own methods only",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",              10, ""},
        {ParseRecordKind::Signal, "aa",  2, 2, "top", CONTAINER_ASSOC, 0,  "top"},
    });

    const std::string text = "module top;\n  aa.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 5), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "exists"));
    CHECK(hasItem(items, "num"));
    CHECK_FALSE(hasItem(items, "push_back")); // queue-only
}

TEST_CASE("CompletionProvider: dot-completion on a dynamic array offers delete but not push/pop",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",                      10, ""},
        {ParseRecordKind::Signal, "arr", 2, 2, "top", CONTAINER_DYNAMIC_ARRAY, 0,  "top"},
    });

    const std::string text = "module top;\n  arr.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "delete"));
    CHECK(hasItem(items, "sort"));
    CHECK_FALSE(hasItem(items, "push_back"));
}

TEST_CASE("CompletionProvider: dot-completion on a fixed-size array excludes delete",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",                    10, ""},
        {ParseRecordKind::Signal, "arr", 2, 2, "top", CONTAINER_FIXED_ARRAY, 0,  "top"},
    });

    const std::string text = "module top;\n  arr.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "size"));
    CHECK(hasItem(items, "sort"));
    CHECK_FALSE(hasItem(items, "delete"));
}

TEST_CASE("CompletionProvider: dot-completion on a string offers string methods",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",               10, ""},
        {ParseRecordKind::Signal, "s",   2, 2, "top", CONTAINER_STRING, 0,  "top"},
    });

    const std::string text = "module top;\n  s.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 4), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "toupper"));
    CHECK(hasItem(items, "len"));
}

TEST_CASE("CompletionProvider: dot-completion on an event offers only 'triggered'",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",              10, ""},
        {ParseRecordKind::Signal, "e",   2, 2, "top", CONTAINER_EVENT, 0,  "top"},
    });

    const std::string text = "module top;\n  e.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 4), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    REQUIRE(items.size() == 1);
    CHECK(items[0].label == "triggered");
    CHECK(static_cast<lsp::CompletionItemKind>(items[0].kind.value()) == lsp::CompletionItemKind::Property);
}

TEST_CASE("CompletionProvider: dot-completion on a mailbox/semaphore/process offers their own methods",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",          10, ""},
        {ParseRecordKind::Signal, "mbx", 2, 2, "top", "mailbox",   0,  "top"},
        {ParseRecordKind::Signal, "sem", 3, 2, "top", "semaphore", 0,  "top"},
        {ParseRecordKind::Signal, "p",   4, 2, "top", "process",   0,  "top"},
    });

    auto mbxResult = CompletionProvider::getCompletion(
        makeParams("/t.sv", 1, 6), f.sdb, "module top;\n  mbx.");
    REQUIRE_FALSE(mbxResult.isNull());
    auto& mbxItems = mbxResult.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(mbxItems, "put"));
    CHECK(hasItem(mbxItems, "get"));
    CHECK_FALSE(hasItem(mbxItems, "status")); // process-only

    auto semResult = CompletionProvider::getCompletion(
        makeParams("/t.sv", 2, 6), f.sdb, "\nmodule top;\n  sem.");
    REQUIRE_FALSE(semResult.isNull());
    auto& semItems = semResult.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(semItems, "try_get"));
    CHECK_FALSE(hasItem(semItems, "put_front")); // not a real method anywhere

    auto pResult = CompletionProvider::getCompletion(
        makeParams("/t.sv", 3, 4), f.sdb, "\n\nmodule top;\n  p.");
    REQUIRE_FALSE(pResult.isNull());
    auto& pItems = pResult.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(pItems, "status"));
    CHECK(hasItem(pItems, "self"));
}

TEST_CASE("CompletionProvider: dot-completion on a class instance unions implicit randomize methods",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",        10, ""},
        {ParseRecordKind::Signal,   "obj",     2, 2, "top",     "MyClass", 0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 5, 7,  "",        "",        8,  ""},
        {ParseRecordKind::Function, "bar",     6, 10, "MyClass", "void",    6,  "MyClass"},
    });

    const std::string text = "module top;\n  obj.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "bar"));         // real declared member, unaffected
    CHECK(hasItem(items, "randomize"));   // synthetic, implicit on every class
    CHECK(hasItem(items, "pre_randomize"));
}

TEST_CASE("CompletionProvider: a class's own declared randomize wins over the synthetic one",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",           10, ""},
        {ParseRecordKind::Signal,   "obj",     2, 2, "top",     "MyClass",    0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 5, 7,  "",        "",           8,  ""},
        {ParseRecordKind::Function, "randomize", 6, 10, "MyClass", "int",      6,  "MyClass"},
    });

    const std::string text = "module top;\n  obj.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    int count = static_cast<int>(std::count_if(items.begin(), items.end(),
        [](const auto& i){ return i.label == "randomize"; }));
    REQUIRE(count == 1);
    auto it = std::find_if(items.begin(), items.end(),
                           [](const auto& i){ return i.label == "randomize"; });
    // The real DB row's kind (Function) wins over the synthetic Method kind.
    CHECK(static_cast<lsp::CompletionItemKind>(it->kind.value()) == lsp::CompletionItemKind::Function);
}

TEST_CASE("CompletionProvider: dot-completion on an unresolved type still fails closed",
          "[completion][dot][builtin]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",          10, ""},
        {ParseRecordKind::Signal, "obj", 2, 10, "top", "NoSuchType", 0, "top"},
    });

    const std::string text = "module top;\n  obj.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE(result.isNull());
}

// ---------------------------------------------------------------------------
// Chained/function-call dot-completion (plan.md §6.14)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: two-level chain resolves through a Signal member",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",   1, 7,  "",      "",      20, ""},
        {ParseRecordKind::Signal,   "obj",   2, 10, "top",   "Outer", 0,  "top"},
        {ParseRecordKind::Class,    "Outer", 5, 7,  "",      "",      8,  ""},
        {ParseRecordKind::Signal,   "child", 6, 10, "Outer", "Inner", 0,  "Outer"},
        {ParseRecordKind::Class,    "Inner", 12, 7, "",      "",      14, ""},
        {ParseRecordKind::Function, "greet", 13, 10, "Inner", "void", 13, "Inner"},
    });

    const std::string text = "module top;\n  obj.child.gr";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 1, 14), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: three-level chain resolves through two function calls",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Function, "make_factory", 1, 0,  "",        "Factory", 3,  ""},
        {ParseRecordKind::Class,    "Factory",      5, 7,  "",        "",        8,  ""},
        {ParseRecordKind::Function, "get_child",    6, 10, "Factory", "Greeter", 6,  "Factory"},
        {ParseRecordKind::Class,    "Greeter",      12, 7, "",        "",        14, ""},
        {ParseRecordKind::Function, "greet",        13, 10, "Greeter", "void",   13, "Greeter"},
    });

    const std::string text = "make_factory().get_child().gr";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: a broken link mid-chain (built-in return type) fails closed",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Function, "get_num", 1, 0, "", "int", 3, ""},
    });

    const std::string text = "get_num().something().x";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("CompletionProvider: this. resolves through the enclosing class, even nested in a method",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Class,    "Widget",    1, 7,  "",       "",        1, ""},
        {ParseRecordKind::Function, "method_a",  1, 10, "Widget", "void",    1, "Widget"},
        {ParseRecordKind::Function, "get_child", 1, 10, "Widget", "Greeter", 1, "Widget"},
        {ParseRecordKind::Class,    "Greeter",   20, 7,  "",       "",       22, ""},
        {ParseRecordKind::Function, "greet",     21, 10, "Greeter", "void",  21, "Greeter"},
    });

    // Cursor is nested inside method_a's own body (both share line 1 here),
    // proving enclosingClassNameAt finds Widget even though the innermost
    // *scope kind* at this position is Function, not Class.
    const std::string text = "this.get_child().gr";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: super. resolves through the parent class, not the child's own override",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        // Base hierarchy, deliberately placed far from line 1 so it never
        // ambiguously overlaps the cursor position used below.
        {ParseRecordKind::Class,    "BaseFactory",     30, 7,  "",            "",             31, ""},
        {ParseRecordKind::Function, "get_child",       30, 10, "BaseFactory", "BaseGreeter",  31, "BaseFactory"},
        {ParseRecordKind::Class,    "BaseGreeter",     40, 7,  "",            "",             41, ""},
        {ParseRecordKind::Function, "greet_from_base", 40, 10, "BaseGreeter", "void",         41, "BaseGreeter"},

        // Factory extends BaseFactory and overrides get_child with a
        // *different* return type -- proves super bypasses this override.
        {ParseRecordKind::Class,    "Factory",           1, 7,  "",        "BaseFactory",  1, ""},
        {ParseRecordKind::Function, "get_child",         1, 10, "Factory", "ChildGreeter", 1, "Factory"},
        {ParseRecordKind::Function, "method_in_factory", 1, 10, "Factory", "void",         1, "Factory"},
        {ParseRecordKind::Class,    "ChildGreeter",      10, 7,  "",            "",             11, ""},
        {ParseRecordKind::Function, "greet_from_child",  10, 10, "ChildGreeter", "void",        11, "ChildGreeter"},
    });

    // Cursor nested inside method_in_factory (line 1, same as Factory's own
    // range here) -- enclosingClassNameAt must resolve to Factory, then
    // `super` must resolve to BaseFactory, not stay on Factory.
    const std::string text = "super.get_child().greet_from_b";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet_from_base"));
    CHECK_FALSE(hasItem(items, "greet_from_child"));
}

TEST_CASE("CompletionProvider: an ordinary object handle offers a method inherited "
          "two extends levels up, not just one (plan.md §6.26)",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        // Class hierarchy deliberately placed far from line 1 (same
        // precaution the super. test above takes) so it never ambiguously
        // overlaps `top`'s own span at the cursor's actual line.
        {ParseRecordKind::Module, "top",         1, 7,  "",     "",             5,  ""},
        {ParseRecordKind::Signal, "obj",         2, 2, "top",  "Child",        0,  "top"},
        {ParseRecordKind::Class,  "Grandparent", 30, 7,  "",     "",             32, ""},
        {ParseRecordKind::Function, "greet",     31, 10, "Grandparent", "void", 31, "Grandparent"},
        {ParseRecordKind::Class,  "Parent",      20, 7,  "",     "Grandparent", 22, ""},
        {ParseRecordKind::Class,  "Child",       10, 7,  "",     "Parent",      12, ""},
    });

    // Before plan.md §6.26, dot-completion only walked a single `extends`
    // hop (and only for the literal `super` keyword) -- `greet`, declared
    // two levels up an ordinary object handle's own class hierarchy, was
    // previously invisible here.
    const std::string text = "module top;\n  obj.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 6), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: a chain ending on a built-in container member reuses §6.13's tables",
          "[completion][dot][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top",    1, 7,  "",      "",         20, ""},
        {ParseRecordKind::Signal, "obj2",   2, 10, "top",   "Widget2",  0,  "top"},
        {ParseRecordKind::Class,  "Widget2", 5, 7, "",      "",         8,  ""},
        {ParseRecordKind::Signal, "mbx",    6, 10, "Widget2", "mailbox", 0, "Widget2"},
    });

    const std::string text = "module top;\n  obj2.mbx.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 11), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "put"));
    CHECK(hasItem(items, "get"));
}

// ---------------------------------------------------------------------------
// Queue/associative-array element access completion (plan.md §6.15)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: indexing a queue of a class resolves to the class's own members",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",
            10, ""},
        {ParseRecordKind::Signal,   "q",       2, 2, "top",     std::string(CONTAINER_QUEUE) + ":MyClass",
            0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 5, 7,  "",        "",
            8,  ""},
        {ParseRecordKind::Function, "bar",     6, 10, "MyClass", "void",
            6,  "MyClass"},
    });

    const std::string text = "module top;\n  q[0].";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 7), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "bar"));
    CHECK_FALSE(hasItem(items, "push_back")); // indexed past the queue, onto the element itself
}

TEST_CASE("CompletionProvider: indexing an associative array of a class resolves to the class's own members",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",
            10, ""},
        {ParseRecordKind::Signal,   "aa",      2, 10, "top",     std::string(CONTAINER_ASSOC) + ":MyClass",
            0,  "top"},
        {ParseRecordKind::Class,    "MyClass", 5, 7,  "",        "",
            8,  ""},
        {ParseRecordKind::Function, "bar",     6, 10, "MyClass", "void",
            6,  "MyClass"},
    });

    const std::string text = "module top;\n  aa[\"k\"].";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 10), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "bar"));
}

TEST_CASE("CompletionProvider: partially indexing a fixed-array-of-queues lands on the queue's own methods",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",
            10, ""},
        {ParseRecordKind::Signal, "arr", 2, 2, "top", std::string(CONTAINER_FIXED_ARRAY) + ":" + CONTAINER_QUEUE + ":MyClass",
            0,  "top"},
        {ParseRecordKind::Class,  "MyClass", 5, 7, "", "", 8, ""},
        {ParseRecordKind::Function, "bar", 6, 10, "MyClass", "void", 6, "MyClass"},
    });

    // Only one index -- arr[i] is still a queue, not yet a MyClass.
    const std::string text = "module top;\n  arr[i].";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 9), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "push_back"));
    CHECK(hasItem(items, "pop_front"));
    CHECK_FALSE(hasItem(items, "bar")); // not yet indexed into the element
}

TEST_CASE("CompletionProvider: fully indexing a fixed-array-of-queues lands on the class's own members",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",
            10, ""},
        {ParseRecordKind::Signal, "arr", 2, 10, "top", std::string(CONTAINER_FIXED_ARRAY) + ":" + CONTAINER_QUEUE + ":MyClass",
            0,  "top"},
        {ParseRecordKind::Class,  "MyClass", 5, 7, "", "", 8, ""},
        {ParseRecordKind::Function, "bar", 6, 10, "MyClass", "void", 6, "MyClass"},
    });

    // Both indices -- arr[i][j] reaches the MyClass element itself.
    const std::string text = "module top;\n  arr[i][j].";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 12), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "bar"));
    CHECK_FALSE(hasItem(items, "push_back"));
}

TEST_CASE("CompletionProvider: over-indexing past the available dimensions fails closed",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",
            10, ""},
        {ParseRecordKind::Signal, "q",   2, 10, "top", std::string(CONTAINER_QUEUE) + ":MyClass",
            0,  "top"},
        {ParseRecordKind::Class,  "MyClass", 5, 7, "", "", 8, ""},
    });

    // q only has one dimension (queue) -- a second index goes past it.
    const std::string text = "module top;\n  q[0][1].";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 10), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("CompletionProvider: indexing a queue of a built-in type still yields no completions",
          "[completion][dot][index]")
{
    // Regression guard, same fail-closed posture as the unindexed §6.13 case.
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",
            10, ""},
        {ParseRecordKind::Signal, "q",   2, 10, "top", CONTAINER_QUEUE, 0, "top"},
    });

    const std::string text = "module top;\n  q[0].";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 7), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("CompletionProvider: a plain (non-indexed) container access is unaffected by indexing support",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 7,  "",    "",
            10, ""},
        {ParseRecordKind::Signal, "q",   2, 2, "top", std::string(CONTAINER_QUEUE) + ":MyClass",
            0,  "top"},
        {ParseRecordKind::Class,  "MyClass", 5, 7, "", "", 8, ""},
        {ParseRecordKind::Function, "bar", 6, 10, "MyClass", "void", 6, "MyClass"},
    });

    const std::string text = "module top;\n  q.";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 4), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "push_back")); // the container's own methods, not MyClass's
    CHECK_FALSE(hasItem(items, "bar"));
}

TEST_CASE("CompletionProvider: an indexed member deep in a chain resolves through the element class",
          "[completion][dot][index]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top",   1, 7,  "",      "",
            20, ""},
        {ParseRecordKind::Signal, "obj",   2, 10, "top",   "Outer",
            0,  "top"},
        {ParseRecordKind::Class,  "Outer", 5, 7,  "",      "",
            8,  ""},
        {ParseRecordKind::Signal, "kids",  6, 10, "Outer", std::string(CONTAINER_QUEUE) + ":Inner",
            0,  "Outer"},
        {ParseRecordKind::Class,  "Inner", 12, 7, "",      "",
            14, ""},
        {ParseRecordKind::Function, "greet", 13, 10, "Inner", "void",
            13, "Inner"},
    });

    const std::string text = "module top;\n  obj.kids[0].gr";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 1, 16), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

// ---------------------------------------------------------------------------
// Dot-completion into a package-nested class's members (plan.md §6.17)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: dot-completion resolves into a class declared inside a package",
          "[completion][dot][package]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",    1, 7,  "",       "",
            20, ""},
        {ParseRecordKind::Signal,   "obj",    2, 2, "top",    "Widget",
            0,  "top"},
        {ParseRecordKind::Class,    "Widget", 5, 7,  "",       "",
            8,  "pkg"},
        {ParseRecordKind::Function, "greet",  6, 10, "Widget", "void",
            6,  "pkg::Widget"},
    });

    const std::string text = "module top;\n  obj.gr";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 8), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: an intermediate chain segment resolves into a package-nested class",
          "[completion][dot][package][chain]")
{
    // Not just the terminal-hop fix (previous test): a member hop (once
    // resolveMemberSegment, now the shared resolver) hit the same bug
    // independently when a package-nested class is reached as a
    // *non-final* segment of a longer chain.
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",   1, 7,  "",      "",
            20, ""},
        {ParseRecordKind::Signal,   "obj",   2, 10, "top",   "Outer",
            0,  "top"},
        {ParseRecordKind::Class,    "Outer", 5, 7,  "",      "",
            8,  ""},
        {ParseRecordKind::Signal,   "child", 6, 10, "Outer", "Inner",
            0,  "Outer"},
        {ParseRecordKind::Class,    "Inner", 11, 9, "",      "",
            14, "pkg"},
        {ParseRecordKind::Function, "greet", 12, 12,"Inner", "void",
            12, "pkg::Inner"},
    });

    const std::string text = "module top;\n  obj.child.gr";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 14), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: this. resolves through a package-nested enclosing class",
          "[completion][dot][package][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Class,    "Widget",   1, 7,  "",       "",     1, "pkg"},
        {ParseRecordKind::Function, "method_a", 1, 10, "Widget", "void", 1, "pkg::Widget"},
        {ParseRecordKind::Function, "greet",    1, 10, "Widget", "void", 1, "pkg::Widget"},
    });

    // Cursor nested inside method_a's own body (both share line 1 here,
    // matching the existing "this. resolves..." test's own convention) --
    // enclosingClassNameAt must still find Widget even though its own
    // `scope` column is "pkg", not "".
    const std::string text = "this.gr";
    auto result = CompletionProvider::getCompletion(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: a top-level (non-package) class still resolves unchanged",
          "[completion][dot][package]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",    1, 7,  "",       "",
            20, ""},
        {ParseRecordKind::Signal,   "obj",    2, 2, "top",    "Widget",
            0,  "top"},
        {ParseRecordKind::Class,    "Widget", 5, 7,  "",       "",
            8,  ""},
        {ParseRecordKind::Function, "greet",  6, 10, "Widget", "void",
            6,  "Widget"},
    });

    const std::string text = "module top;\n  obj.gr";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 8), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "greet"));
}

TEST_CASE("CompletionProvider: an intermediate hop resolving to a bogus type still fails closed",
          "[completion][dot][package][chain]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top",   1, 7,  "",     "",
            20, ""},
        {ParseRecordKind::Signal, "obj",   2, 10, "top",  "Outer",
            0,  "top"},
        {ParseRecordKind::Class,  "Outer", 5, 7,  "",     "",
            8,  ""},
        {ParseRecordKind::Signal, "child", 6, 10, "Outer","NoSuchType",
            0,  "Outer"},
    });

    const std::string text = "module top;\n  obj.child.gr";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 14), f.sdb, text);
    REQUIRE(result.isNull());
}

// ---------------------------------------------------------------------------
// Context-aware keyword completion (plan.md §6.9, extended: legality rules)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: keyword items carry the Keyword kind", "[completion][keyword]")
{
    Fixture f;
    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    auto it = std::find_if(items.begin(), items.end(),
                           [](const auto& i){ return i.label == "module"; });
    REQUIRE(it != items.end());
    CHECK(static_cast<lsp::CompletionItemKind>(it->kind.value()) == lsp::CompletionItemKind::Keyword);
}

TEST_CASE("CompletionProvider: top-level scope offers design-unit keywords, not statement keywords",
          "[completion][keyword]")
{
    Fixture f;
    // No DB symbols at all -> scopeKindAtPosition is "" (top level).
    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "module"));
    CHECK(hasItem(items, "package"));
    CHECK_FALSE(hasItem(items, "if"));
    CHECK_FALSE(hasItem(items, "endmodule"));
}

TEST_CASE("CompletionProvider: inside a module body, design-unit keywords are illegal (can't nest modules)",
          "[completion][keyword]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""},
    });

    // Cursor at line 9 (0-based) -> 1-based line 10, inside "top"'s body.
    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 9, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "always"));
    CHECK(hasItem(items, "initial"));
    CHECK(hasItem(items, "endmodule"));
    CHECK(hasItem(items, "function"));
    CHECK_FALSE(hasItem(items, "module"));   // modules can't nest
    CHECK_FALSE(hasItem(items, "endclass")); // not inside a class
}

TEST_CASE("CompletionProvider: inside a class body (not a method), bare statement keywords are illegal",
          "[completion][keyword]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 0,  "",    "", 20, ""},
        {ParseRecordKind::Class,  "Cls", 3, 0,  "top", "", 12, "top"},
    });

    // Cursor at line 4 (0-based) -> 1-based line 5, inside Cls's body,
    // outside any of its methods.
    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 4, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "endclass"));
    CHECK(hasItem(items, "function"));
    CHECK(hasItem(items, "rand"));
    CHECK(hasItem(items, "local"));
    CHECK_FALSE(hasItem(items, "module"));
    CHECK_FALSE(hasItem(items, "if"));      // bare statements aren't legal directly in a class body
    CHECK_FALSE(hasItem(items, "begin"));
}

TEST_CASE("CompletionProvider: inside a function body, statement keywords are legal",
          "[completion][keyword]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",  1, 0, "",    "", 20, ""},
        {ParseRecordKind::Function, "calc", 5, 0, "top", "", 10, "top"},
    });

    // Cursor at line 6 (0-based) -> 1-based line 7, inside calc's body.
    const std::string text = " ";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 6, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "if"));
    CHECK(hasItem(items, "begin"));
    CHECK(hasItem(items, "return"));
    CHECK(hasItem(items, "endfunction"));
    CHECK_FALSE(hasItem(items, "module"));
    CHECK_FALSE(hasItem(items, "class"));
}

TEST_CASE("CompletionProvider: keyword fuzzy-prefix match still respects context",
          "[completion][keyword]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module, "top", 1, 0, "", "", 20, ""},
    });

    // Typing "mod" inside top's body (line 9, 0-based -> 1-based 10) should
    // not surface "module" -- illegal here regardless of prefix match.
    const std::string text = "mod";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 9, 3), f.sdb, text);
    if (!result.isNull()) {
        auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
        CHECK_FALSE(hasItem(items, "module"));
    }
}

// plan.md §6.30 step A: a receiver declared with a package-qualified type
// completes on *that* package's class. Before the type's qualifier was kept
// in `detail`, `pkg_b::Item x;` recorded plain "Item", which resolved to the
// first-declared pkg_a::Item and offered its members instead.
TEST_CASE("CompletionProvider: dot-completion on a pkg::-qualified receiver offers that "
          "package's class members", "[completion][dot][phase6.30]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    ctrl.compile("/pkgs.sv",
        "package pkg_a;\n"
        "  class Item; int only_in_a; endclass\n"
        "endpackage\n"
        "package pkg_b;\n"
        "  class Item; int only_in_b; endclass\n"
        "endpackage\n");
    const std::string text =
        "module top;\n"
        "  pkg_b::Item x;\n"
        "  pkg_b::Item q[$];\n"
        "  initial begin\n"
        "    x.\n"      // line 4
        "    q[0].\n"   // line 5
        "  end\n"
        "endmodule\n";
    ctrl.compile("/top.sv", text);

    for (unsigned line : {4u, 5u}) {
        const unsigned col = line == 4 ? 6 : 9;
        auto result = CompletionProvider::getCompletion(makeParams("/top.sv", line, col), f.sdb, text);
        REQUIRE_FALSE(result.isNull());
        const auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
        CHECK(hasItem(items, "only_in_b"));
        CHECK_FALSE(hasItem(items, "only_in_a"));
    }
}

TEST_CASE("CompletionProvider: typedefs, enum literals and genvars are offered; struct members "
          "are not offered as bare names",
          "[completion][phase6.30]")
{
    // plan.md §6.30 step C records these kinds. A struct member is only
    // reachable through its variable (`s.cmp_hi`), so it must not show up
    // among the bare names visible in the module.
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "module cmp_m;\n"
        "  typedef logic [7:0] cmp_byte_t;\n"
        "  enum {CMP_IDLE, CMP_BUSY} st;\n"
        "  struct packed { logic [3:0] cmp_hi; logic [3:0] cmp_lo; } s;\n"
        "  genvar cmp_g;\n"
        "  initial begin\n"
        "    cmp\n"   // line 6
        "  end\n"
        "endmodule\n";
    ctrl.compile("/cmp.sv", text);

    auto result = CompletionProvider::getCompletion(makeParams("/cmp.sv", 6, 7), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    const auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "cmp_byte_t"));
    CHECK(hasItem(items, "CMP_IDLE"));
    CHECK(hasItem(items, "CMP_BUSY"));
    CHECK(hasItem(items, "cmp_g"));
    CHECK_FALSE(hasItem(items, "cmp_hi"));
    CHECK_FALSE(hasItem(items, "cmp_lo"));
}

TEST_CASE("CompletionProvider: inside an out-of-class body, the class's own fields are "
          "offered and an extern method is listed once",
          "[completion][phase6.30][outofclass]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package occ_p;\n"
        "  class OccBase; int occ_inh; endclass\n"
        "  class OccC extends OccBase;\n"
        "    int occ_fld;\n"
        "    extern function void occ_run();\n"
        "  endclass\n"
        "  function void OccC::occ_run();\n"
        "    occ\n"   // line 7
        "  endfunction\n"
        "endpackage\n";
    ctrl.compile("/occ.sv", text);

    auto result = CompletionProvider::getCompletion(makeParams("/occ.sv", 7, 7), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    const auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "occ_fld"));
    CHECK(hasItem(items, "occ_inh"));
    CHECK(std::count_if(items.begin(), items.end(), [](const lsp::CompletionItem& i) {
              return std::string(i.label) == "occ_run";
          }) == 1);
}

TEST_CASE("CompletionProvider: a constructor is never offered as a member, and its arguments "
          "don't leak into the class",
          "[completion][ctor]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package cc_p;\n"
        "  class CcA;\n"
        "    int cc_fld;\n"
        "    function new(int cc_arg); endfunction\n"
        "    function void cc_m();\n"
        "      cc\n"            // line 5
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n"
        "module cc_top;\n"
        "  cc_p::CcA h = new(1);\n"
        "  initial h.\n"     // line 11
        "endmodule\n";
    ctrl.compile("/cc.sv", text);

    auto dot = CompletionProvider::getCompletion(makeParams("/cc.sv", 11, 12), f.sdb, text);
    REQUIRE_FALSE(dot.isNull());
    const auto& members = dot.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(members, "cc_fld"));
    CHECK(hasItem(members, "cc_m"));
    CHECK_FALSE(hasItem(members, "new"));
    CHECK_FALSE(hasItem(members, "cc_arg"));

    auto bare = CompletionProvider::getCompletion(makeParams("/cc.sv", 5, 8), f.sdb, text);
    REQUIRE_FALSE(bare.isNull());
    const auto& items = bare.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "cc_fld"));
    CHECK_FALSE(hasItem(items, "cc_arg"));
}

TEST_CASE("CompletionProvider: dot-completion through a class typedef offers the class's members",
          "[completion][dot][typedef]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package tdc_p;\n"
        "  class TdcC; int tdc_fld; function void tdc_m(); endfunction endclass\n"
        "  typedef TdcC tdc_alias_t;\n"
        "  typedef tdc_alias_t tdc_alias2_t;\n"
        "endpackage\n"
        "class TdcDecoy; int tdc_decoy_fld; endclass\n"
        "module tdc_top;\n"
        "  import tdc_p::*;\n"
        "  tdc_alias2_t h;\n"
        "  initial h.\n"     // line 9
        "endmodule\n";
    ctrl.compile("/tdc.sv", text);

    auto dot = CompletionProvider::getCompletion(makeParams("/tdc.sv", 9, 12), f.sdb, text);
    REQUIRE_FALSE(dot.isNull());
    const auto& items = dot.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "tdc_fld"));
    CHECK(hasItem(items, "tdc_m"));
    CHECK(hasItem(items, "randomize"));
    CHECK_FALSE(hasItem(items, "tdc_decoy_fld"));
}

TEST_CASE("CompletionProvider: bare-name completion inside a class offers inherited members, "
          "in in-class and out-of-class bodies",
          "[completion][phase6.30][inherited]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package inh_p;\n"
        "  class InhBase;\n"
        "    int inh_base_fld;\n"
        "    function void inh_base_m(); endfunction\n"
        "  endclass\n"
        "  class InhMid extends InhBase;\n"
        "    int inh_mid_fld;\n"
        "  endclass\n"
        "  class InhC extends InhMid;\n"
        "    int inh_own;\n"
        "    function void run();\n"
        "      inh\n"            // line 11
        "    endfunction\n"
        "    extern function void ooc();\n"
        "  endclass\n"
        "  function void InhC::ooc();\n"
        "    inh\n"              // line 16
        "  endfunction\n"
        "  class InhUnrelated; int inh_unrelated_fld; endclass\n"
        "endpackage\n";
    ctrl.compile("/inh.sv", text);

    for (auto [line, col] : {std::pair{11u, 9u}, std::pair{16u, 7u}}) {
        auto result = CompletionProvider::getCompletion(makeParams("/inh.sv", line, col), f.sdb, text);
        REQUIRE_FALSE(result.isNull());
        const auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
        CHECK(hasItem(items, "inh_own"));
        CHECK(hasItem(items, "inh_mid_fld"));
        CHECK(hasItem(items, "inh_base_fld"));
        CHECK(hasItem(items, "inh_base_m"));
        CHECK_FALSE(hasItem(items, "inh_unrelated_fld"));
    }
}

TEST_CASE("CompletionProvider: bare-name completion skips an ancestor's local members, "
          "keeps protected ones and the class's own local ones",
          "[completion][access][inherited]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package lacc_p;\n"
        "  class LaccBase;\n"
        "    local int lacc_base_loc;\n"
        "    protected int lacc_base_prot;\n"
        "    int lacc_base_pub;\n"
        "    local function void lacc_base_lf(); endfunction\n"
        "    extern local function void lacc_base_ext();\n"
        "    function void base_run();\n"
        "      lacc\n"            // line 8
        "    endfunction\n"
        "  endclass\n"
        "  function void LaccBase::lacc_base_ext(); endfunction\n"
        "  class LaccMid extends LaccBase;\n"
        "    local int lacc_mid_loc;\n"
        "  endclass\n"
        "  class LaccC extends LaccMid;\n"
        "    local int lacc_own_loc;\n"
        "    function void run();\n"
        "      lacc\n"            // line 18
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n";
    ctrl.compile("/lacc.sv", text);

    auto labels = [&](unsigned line) {
        auto result = CompletionProvider::getCompletion(makeParams("/lacc.sv", line, 10), f.sdb, text);
        REQUIRE_FALSE(result.isNull());
        return result.get<lsp::Array<lsp::CompletionItem>>();
    };
    const auto derived = labels(18);
    CHECK(hasItem(derived, "lacc_own_loc"));
    CHECK(hasItem(derived, "lacc_base_prot"));
    CHECK(hasItem(derived, "lacc_base_pub"));
    CHECK_FALSE(hasItem(derived, "lacc_mid_loc"));
    CHECK_FALSE(hasItem(derived, "lacc_base_loc"));
    CHECK_FALSE(hasItem(derived, "lacc_base_lf"));
    CHECK_FALSE(hasItem(derived, "lacc_base_ext")); // the body row has no qualifier

    const auto base = labels(8);
    CHECK(hasItem(base, "lacc_base_loc"));
    CHECK(hasItem(base, "lacc_base_lf"));
    CHECK(hasItem(base, "lacc_base_ext"));
}

TEST_CASE("CompletionProvider: dot-completion offers local/protected members only where "
          "the cursor's class may access them",
          "[completion][dot][access]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    // Compiled with complete statements; each query cuts its line right
    // after the dot (the prefix would otherwise take the word after it).
    const std::string text =
        "package dacc_p;\n"                                  // 0
        "  class DaccBase;\n"
        "    local int dacc_loc;\n"
        "    protected int dacc_prot;\n"
        "    int dacc_pub;\n"
        "    int dacc_shadow;\n"                             // 5
        "    DaccBase peer;\n"
        "    extern local function void dacc_ext();\n"
        "    function void same();\n"
        "      peer.dacc_pub = 1;\n"                         // 9
        "    endfunction\n"
        "    class DaccNested;\n"
        "      DaccBase nb;\n"
        "      function void nest();\n"
        "        nb.dacc_pub = 1;\n"                         // 14
        "      endfunction\n"
        "    endclass\n"
        "  endclass\n"
        "  function void DaccBase::dacc_ext();\n"
        "    this.dacc_pub = 1;\n"                           // 19
        "  endfunction\n"
        "  class DaccDerived extends DaccBase;\n"
        "    local int dacc_shadow;\n"
        "    DaccBase ob;\n"
        "    function void run();\n"
        "      super.dacc_pub = 1;\n"                        // 25
        "      this.dacc_pub = 1;\n"                         // 26
        "      ob.dacc_pub = 1;\n"                           // 27
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n"
        "module dacc_top;\n"
        "  dacc_p::DaccDerived dd;\n"
        "  dacc_p::DaccBase db;\n"
        "  initial begin\n"
        "    dd.dacc_pub = 1;\n"                             // 35
        "    db.dacc_pub = 1;\n"                             // 36
        "  end\n"
        "endmodule\n";
    ctrl.compile("/dacc.sv", text);

    auto itemsAt = [&](unsigned line) {
        size_t start = 0;
        for (unsigned i = 0; i < line; ++i) start = text.find('\n', start) + 1;
        const size_t dot = text.find('.', start);
        const std::string query = text.substr(0, dot + 1) + text.substr(text.find('\n', dot));
        auto result = CompletionProvider::getCompletion(
            makeParams("/dacc.sv", line, static_cast<unsigned>(dot + 1 - start)), f.sdb, query);
        REQUIRE_FALSE(result.isNull());
        return result.get<lsp::Array<lsp::CompletionItem>>();
    };

    SECTION("inside the class, through another instance: everything") {
        const auto items = itemsAt(9);
        CHECK(hasItem(items, "dacc_loc"));
        CHECK(hasItem(items, "dacc_prot"));
        CHECK(hasItem(items, "dacc_ext"));
        CHECK(hasItem(items, "dacc_pub"));
    }
    SECTION("a nested class has its containing class's access") {
        const auto items = itemsAt(14);
        CHECK(hasItem(items, "dacc_loc"));
        CHECK(hasItem(items, "dacc_prot"));
    }
    SECTION("an out-of-class body of the declaring class") {
        const auto items = itemsAt(19);
        CHECK(hasItem(items, "dacc_loc"));
        CHECK(hasItem(items, "dacc_ext"));
    }
    SECTION("a subclass: the parent's protected members, not its local ones") {
        for (unsigned line : {25u, 27u}) {
            const auto items = itemsAt(line);
            CHECK(hasItem(items, "dacc_prot"));
            CHECK(hasItem(items, "dacc_pub"));
            CHECK(hasItem(items, "dacc_shadow")); // DaccBase's public one
            CHECK_FALSE(hasItem(items, "dacc_loc"));
            CHECK_FALSE(hasItem(items, "dacc_ext"));
        }
        const auto self = itemsAt(26);
        CHECK(hasItem(self, "dacc_shadow")); // its own local one
        CHECK(hasItem(self, "dacc_prot"));
        CHECK_FALSE(hasItem(self, "dacc_loc"));
    }
    SECTION("outside any class: public members only") {
        const auto base = itemsAt(36);
        CHECK(hasItem(base, "dacc_pub"));
        CHECK(hasItem(base, "dacc_shadow"));
        CHECK_FALSE(hasItem(base, "dacc_loc"));
        CHECK_FALSE(hasItem(base, "dacc_prot"));
        CHECK_FALSE(hasItem(base, "dacc_ext"));
        CHECK(hasItem(base, "randomize"));

        // DaccDerived's local dacc_shadow hides DaccBase's public one: the
        // name isn't offered at all.
        const auto derived = itemsAt(35);
        CHECK(hasItem(derived, "dacc_pub"));
        CHECK_FALSE(hasItem(derived, "dacc_shadow"));
        CHECK_FALSE(hasItem(derived, "dacc_prot"));
    }
}

TEST_CASE("CompletionProvider: a function/task argument is a dot receiver, dimensions included",
          "[completion][dot][port]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "class ArgItem;\n"                                                  // 0
        "  int ai_fld;\n"
        "  function void ai_m(); endfunction\n"
        "endclass\n"
        "class ArgUser;\n"
        "  function void use_it(ArgItem it, ref ArgItem q[$], ArgItem fa[4], string s);\n"
        "    it.ai_fld = 1;\n"                                              // 6
        "    q.push_back(it);\n"                                            // 7
        "    q[0].ai_fld = 1;\n"                                            // 8
        "    fa[1].ai_fld = 1;\n"                                           // 9
        "    s.len();\n"                                                    // 10
        "  endfunction\n"
        "  extern function void ext(ArgItem other);\n"
        "endclass\n"
        "function void ArgUser::ext(ArgItem other);\n"
        "  other.ai_m();\n"                                                 // 15
        "endfunction\n";
    ctrl.compile("/arg.sv", text);

    // Cuts the line right after its last '.', as in the access case above.
    auto itemsAt = [&](unsigned line) {
        size_t start = 0;
        for (unsigned i = 0; i < line; ++i) start = text.find('\n', start) + 1;
        const size_t end = text.find('\n', start);
        const size_t dot = text.rfind('.', end);
        const std::string query = text.substr(0, dot + 1) + text.substr(end);
        auto result = CompletionProvider::getCompletion(
            makeParams("/arg.sv", line, static_cast<unsigned>(dot + 1 - start)), f.sdb, query);
        REQUIRE_FALSE(result.isNull());
        return result.get<lsp::Array<lsp::CompletionItem>>();
    };

    for (unsigned line : {6u, 8u, 9u, 15u}) {
        const auto items = itemsAt(line);
        CHECK(hasItem(items, "ai_fld"));
        CHECK(hasItem(items, "ai_m"));
        CHECK_FALSE(hasItem(items, "push_back"));
    }
    const auto queue = itemsAt(7);
    CHECK(hasItem(queue, "push_back"));
    CHECK_FALSE(hasItem(queue, "ai_fld"));
    const auto str = itemsAt(10);
    CHECK(hasItem(str, "len"));
    CHECK_FALSE(hasItem(str, "ai_fld"));
}

TEST_CASE("CompletionProvider: an overriding member is offered once, not also from the parent",
          "[completion][phase6.30][inherited]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "class OvB; virtual function void ov_m(); endfunction endclass\n"
        "class OvC extends OvB;\n"
        "  virtual function void ov_m(); endfunction\n"
        "  function void run();\n"
        "    ov_\n"             // line 4
        "  endfunction\n"
        "endclass\n";
    ctrl.compile("/ov.sv", text);
    auto result = CompletionProvider::getCompletion(makeParams("/ov.sv", 4, 7), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    const auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(std::count_if(items.begin(), items.end(),
                        [](const lsp::CompletionItem& i) { return i.label == "ov_m"; }) == 1);
}

TEST_CASE("CompletionProvider: nothing is offered inside a comment or string literal",
          "[completion][comment]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "class CmtW; int cmt_fld; function void put(); endfunction endclass\n"
        "module cmt_top;\n"
        "  CmtW w;\n"
        "  int cmt_x; // cmt\n"                 // line 3
        "  /* w. */\n"                          // line 4
        "  initial $display(\"cmt w.\");\n"   // line 5
        "  initial cmt_x = 1;\n"                // line 6
        "endmodule\n";
    ctrl.compile("/cmt.sv", text);

    auto at = [&](unsigned line, unsigned col) {
        return CompletionProvider::getCompletion(makeParams("/cmt.sv", line, col), f.sdb, text);
    };
    CHECK(at(3, 18).isNull());   // end of `// cmt`
    CHECK(at(4, 7).isNull());    // after `w.` in a block comment
    CHECK(at(5, 26).isNull());   // after `w.` in a string
    // Control: the same prefix in code still completes.
    auto code = at(6, 13);
    REQUIRE_FALSE(code.isNull());
    CHECK(hasItem(code.get<lsp::Array<lsp::CompletionItem>>(), "cmt_x"));
}

TEST_CASE("CompletionProvider: super. inside a constructor offers the parent's new",
          "[completion][dot][ctor]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package sn_p;\n"
        "  class SnBase;\n"
        "    int sn_fld;\n"
        "    function new(int sn_arg); endfunction\n"
        "  endclass\n"
        "  class SnC extends SnBase;\n"
        "    function new();\n"
        "      super.\n"         // line 7
        "    endfunction\n"
        "    function void sn_m();\n"
        "      super.\n"         // line 10
        "    endfunction\n"
        "  endclass\n"
        "  class SnPlainBase; endclass\n"
        "  class SnD extends SnPlainBase;\n"
        "    function new();\n"
        "      super.\n"         // line 16
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n";
    ctrl.compile("/sn.sv", text);

    auto inCtor = CompletionProvider::getCompletion(makeParams("/sn.sv", 7, 12), f.sdb, text);
    REQUIRE_FALSE(inCtor.isNull());
    const auto& items = inCtor.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "sn_fld"));
    auto it = std::find_if(items.begin(), items.end(),
                           [](const auto& i) { return i.label == "new"; });
    REQUIRE(it != items.end());
    CHECK(it->data.has_value()); // the parent's own constructor row, for resolve

    // `super.new` is legal only in a constructor.
    auto inMethod = CompletionProvider::getCompletion(makeParams("/sn.sv", 10, 12), f.sdb, text);
    REQUIRE_FALSE(inMethod.isNull());
    CHECK_FALSE(hasItem(inMethod.get<lsp::Array<lsp::CompletionItem>>(), "new"));

    // A parent with no explicit constructor still has the implicit one.
    auto implicit = CompletionProvider::getCompletion(makeParams("/sn.sv", 16, 12), f.sdb, text);
    REQUIRE_FALSE(implicit.isNull());
    CHECK(hasItem(implicit.get<lsp::Array<lsp::CompletionItem>>(), "new"));
}

TEST_CASE("CompletionProvider: super. in a block inside a constructor still offers new",
          "[completion][dot][ctor][block]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "class SnbBase;\n"
        "  function new(int a); endfunction\n"
        "endclass\n"
        "class SnbC extends SnbBase;\n"
        "  function new();\n"
        "    begin\n"
        "      int snb_t;\n"
        "      super.\n"         // line 7
        "    end\n"
        "  endfunction\n"
        "endclass\n";
    ctrl.compile("/snb.sv", text);
    auto r = CompletionProvider::getCompletion(makeParams("/snb.sv", 7, 12), f.sdb, text);
    REQUIRE_FALSE(r.isNull());
    CHECK(hasItem(r.get<lsp::Array<lsp::CompletionItem>>(), "new"));
}

TEST_CASE("CompletionProvider: an out-of-class body in another file offers the class's own "
          "members",
          "[completion][phase6.30][outofclass]")
{
    const auto dir = std::filesystem::temp_directory_path() / "svlsp_test_completion_xf";
    std::filesystem::create_directories(dir);
    const std::string cls = (dir / "xf_c.svh").string();
    const std::string impl = (dir / "xf_c_impl.svh").string();
    const std::string pkg = (dir / "xf_pkg.sv").string();
    const std::string implText =
        "function void XfC::xf_run();\n"
        "  xf\n"   // line 1
        "endfunction\n";
    std::ofstream(cls) << "class XfBase; int xf_inh; endclass\n"
                          "class XfC extends XfBase;\n"
                          "  int xf_fld;\n"
                          "  extern function void xf_run();\n"
                          "endclass\n";
    std::ofstream(impl) << implText;
    const std::string pkgText = "package xf_p;\n"
                                "  `include \"xf_c.svh\"\n"
                                "  `include \"xf_c_impl.svh\"\n"
                                "endpackage\n";
    std::ofstream(pkg) << pkgText;

    Fixture f;
    CompilationController ctrl{f.sdb};
    ctrl.compile(pkg, pkgText);

    auto result = CompletionProvider::getCompletion(makeParams(impl, 1, 4), f.sdb, implText);
    REQUIRE_FALSE(result.isNull());
    const auto& items = result.get<lsp::Array<lsp::CompletionItem>>();
    CHECK(hasItem(items, "xf_fld"));
    CHECK(hasItem(items, "xf_inh"));
    CHECK(hasItem(items, "xf_run"));
}

// plan.md §6.30, "Follow-up: struct, interface and hierarchical receivers":
// dot completion goes through the shared resolver, so a struct, an
// interface (instance, virtual interface, interface port), a module
// instance and a named block offer their own members.
TEST_CASE("CompletionProvider: struct, interface and hierarchical receivers offer their members",
          "[completion][dot][receivers]")
{
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "package rcvc_p;\n"
        "  typedef struct { int hi; int lo; } pair_t;\n"
        "  typedef struct { pair_t inner; int n; } outer_t;\n"
        "endpackage\n"
        "interface rcvc_if(input logic clk);\n"
        "  logic valid;\n"
        "  task automatic drive(); endtask\n"
        "endinterface\n"
        "module rcvc_sub(input logic a);\n"
        "  logic sig;\n"
        "endmodule\n"
        "module rcvc_top(rcvc_if pb);\n"
        "  import rcvc_p::*;\n"
        "  logic clk;\n"
        "  pair_t s;\n"
        "  outer_t o;\n"
        "  pair_t arr[4];\n"
        "  rcvc_if u_if(clk);\n"
        "  virtual rcvc_if vif;\n"
        "  rcvc_sub u_sub(.a(clk));\n"
        "  initial begin : blk\n"
        "    int bx;\n"
        "  end\n"
        "  initial begin\n"
        "  end\n"         // line 24: each probe is inserted before it
        "endmodule\n";
    ctrl.compile("/rcvc.sv", text);

    // The DB comes from the clean text; completion runs on a copy with one
    // probe line (24) typed into the initial block, as while editing.
    const std::map<unsigned, std::string> probes = {
        {24u, "s."}, {25u, "o.inner."}, {26u, "arr[0]."}, {27u, "u_if."}, {28u, "vif."},
        {29u, "pb."}, {30u, "u_sub."}, {31u, "blk."}, {32u, "arr."}};
    auto itemsAt = [&](unsigned probe) {
        size_t lineStart = 0;
        for (unsigned l = 0; l < 24; ++l) lineStart = text.find('\n', lineStart) + 1;
        const std::string typed = "    " + probes.at(probe);
        const std::string doc = text.substr(0, lineStart) + typed + "\n" + text.substr(lineStart);
        auto result = CompletionProvider::getCompletion(
            makeParams("/rcvc.sv", 24, static_cast<unsigned>(typed.size())), f.sdb, doc);
        INFO("probe " << probes.at(probe));
        REQUIRE_FALSE(result.isNull());
        return result.get<lsp::Array<lsp::CompletionItem>>();
    };

    for (unsigned probe : {24u, 25u, 26u}) {
        const auto items = itemsAt(probe);
        CHECK(hasItem(items, "hi"));
        CHECK(hasItem(items, "lo"));
        CHECK_FALSE(hasItem(items, "n"));
        CHECK_FALSE(hasItem(items, "sig"));
    }
    for (unsigned probe : {27u, 28u, 29u}) {
        const auto items = itemsAt(probe);
        CHECK(hasItem(items, "valid"));
        CHECK(hasItem(items, "drive"));
        CHECK(hasItem(items, "clk"));
        CHECK_FALSE(hasItem(items, "hi"));
    }
    {
        const auto items = itemsAt(30);
        CHECK(hasItem(items, "sig"));
        CHECK(hasItem(items, "a"));
        CHECK_FALSE(hasItem(items, "valid"));
    }
    {
        const auto items = itemsAt(31);
        CHECK(hasItem(items, "bx"));
        CHECK_FALSE(hasItem(items, "sig"));
    }
    {
        // An array of structs offers the array's own methods, not the fields.
        const auto items = itemsAt(32);
        CHECK(hasItem(items, "size"));
        CHECK_FALSE(hasItem(items, "hi"));
    }
}

TEST_CASE("CompletionProvider: a module or block receiver offers its named blocks",
          "[completion][dot][receivers]")
{
    // §6.30 receivers follow-up, disclosed: `u_sub.` skipped every Block.
    // Named ones are hierarchy (`u_sub.g_blk.w`); an unnamed one ("@l.c")
    // can't be named.
    Fixture f;
    CompilationController ctrl{f.sdb};
    const std::string text =
        "module nbc_sub;\n"
        "  logic sig;\n"
        "  if (1) begin : g_blk\n"
        "    logic w;\n"
        "  end\n"
        "  initial begin : run\n"
        "    begin : inner\n"
        "      int ix;\n"
        "    end\n"
        "  end\n"
        "  initial begin\n"
        "    int anon;\n"
        "  end\n"
        "endmodule\n"
        "module nbc_top;\n"
        "  nbc_sub u_sub();\n"
        "  initial begin\n"
        "  end\n"          // line 17: each probe is inserted before it
        "endmodule\n";
    ctrl.compile("/nbc.sv", text);

    auto itemsAt = [&](const std::string& probe) {
        size_t lineStart = 0;
        for (unsigned l = 0; l < 17; ++l) lineStart = text.find('\n', lineStart) + 1;
        const std::string typed = "    " + probe;
        const std::string doc = text.substr(0, lineStart) + typed + "\n" + text.substr(lineStart);
        auto result = CompletionProvider::getCompletion(
            makeParams("/nbc.sv", 17, static_cast<unsigned>(typed.size())), f.sdb, doc);
        INFO("probe " << probe);
        REQUIRE_FALSE(result.isNull());
        return result.get<lsp::Array<lsp::CompletionItem>>();
    };
    auto kindOf = [](const lsp::Array<lsp::CompletionItem>& items, const std::string& label)
        -> std::optional<lsp::CompletionItemKind> {
        for (const auto& i : items)
            if (i.label == label && i.kind) return lsp::CompletionItemKind(*i.kind);
        return std::nullopt;
    };

    {
        const auto items = itemsAt("u_sub.");
        CHECK(hasItem(items, "sig"));
        CHECK(hasItem(items, "g_blk"));
        CHECK(hasItem(items, "run"));
        CHECK(kindOf(items, "g_blk") == lsp::CompletionItemKind::Module);
        for (const auto& i : items) CHECK(i.label.rfind('@', 0) != 0);
    }
    {
        const auto items = itemsAt("u_sub.run.");
        CHECK(hasItem(items, "inner"));
        CHECK_FALSE(hasItem(items, "sig"));
    }
    {
        // Bare completion doesn't offer block names.
        const auto items = itemsAt("");
        CHECK_FALSE(hasItem(items, "g_blk"));
    }
}
