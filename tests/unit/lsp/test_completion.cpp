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

TEST_CASE("CompletionProvider: null when no symbols visible", "[completion]")
{
    Fixture f;
    // Empty DB — no symbols to complete
    const std::string text = "module top;\n  \nendmodule";
    auto result = CompletionProvider::getCompletion(makeParams("/t.sv", 1, 2), f.sdb, text);
    REQUIRE(result.isNull());
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
    REQUIRE(items.size() == 2);
    CHECK(items[0].label == "report_id");
    CHECK(items[1].label == "xxrepxx");
    // Ranking must also be encoded in sortText, for clients that re-sort by it.
    REQUIRE(items[0].sortText.has_value());
    REQUIRE(items[1].sortText.has_value());
    CHECK(*items[0].sortText < *items[1].sortText);
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
