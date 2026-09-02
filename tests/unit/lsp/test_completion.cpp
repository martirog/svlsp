#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <algorithm>

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
// Dot / member-access completion (plan.md §6.10)
// ---------------------------------------------------------------------------

TEST_CASE("CompletionProvider: dot-completion narrows to the object's class members", "[completion][dot]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",        10, ""},
        {ParseRecordKind::Signal,   "foo",     2, 10, "top",     "MyClass", 0,  "top"},
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

TEST_CASE("CompletionProvider: dot-completion filters class members by typed prefix", "[completion][dot]")
{
    Fixture f;
    f.sdb.replaceSymbols(f.sdb.upsertFile("/t.sv", "h"), {
        {ParseRecordKind::Module,   "top",     1, 7,  "",        "",        10, ""},
        {ParseRecordKind::Signal,   "foo",     2, 10, "top",     "MyClass", 0,  "top"},
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
        {ParseRecordKind::Signal, "q",   2, 10, "top", CONTAINER_QUEUE, 0,  "top"},
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
        {ParseRecordKind::Signal, "aa",  2, 10, "top", CONTAINER_ASSOC, 0,  "top"},
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
        {ParseRecordKind::Signal, "arr", 2, 10, "top", CONTAINER_DYNAMIC_ARRAY, 0,  "top"},
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
        {ParseRecordKind::Signal, "arr", 2, 10, "top", CONTAINER_FIXED_ARRAY, 0,  "top"},
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
        {ParseRecordKind::Signal, "s",   2, 10, "top", CONTAINER_STRING, 0,  "top"},
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
        {ParseRecordKind::Signal, "e",   2, 10, "top", CONTAINER_EVENT, 0,  "top"},
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
        {ParseRecordKind::Signal, "mbx", 2, 10, "top", "mailbox",   0,  "top"},
        {ParseRecordKind::Signal, "sem", 3, 10, "top", "semaphore", 0,  "top"},
        {ParseRecordKind::Signal, "p",   4, 10, "top", "process",   0,  "top"},
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
        {ParseRecordKind::Signal,   "obj",     2, 10, "top",     "MyClass", 0,  "top"},
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
        {ParseRecordKind::Signal,   "obj",     2, 10, "top",     "MyClass",    0,  "top"},
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
