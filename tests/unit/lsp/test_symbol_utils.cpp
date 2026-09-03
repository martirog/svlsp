#include <catch2/catch_test_macros.hpp>
#include "lsp/symbol_utils.h"
#include "db/symbol_database.h"
#include <vector>

// ---------------------------------------------------------------------------
// wordAtPosition
// ---------------------------------------------------------------------------

TEST_CASE("wordAtPosition: extracts word at cursor", "[symbol_utils]")
{
    // 0-based line 0, "module adder"
    //                   0123456789...
    CHECK(wordAtPosition("module adder", 0, 0) == "module");
    CHECK(wordAtPosition("module adder", 0, 2) == "module"); // mid-word
    CHECK(wordAtPosition("module adder", 0, 7) == "adder");
    CHECK(wordAtPosition("module adder", 0, 11) == "adder");  // last char
}

TEST_CASE("wordAtPosition: works on multi-line text", "[symbol_utils]")
{
    const std::string src = "module adder (\n  input clk\n);\nendmodule";
    // line 0: "module adder ("
    CHECK(wordAtPosition(src, 0, 7)  == "adder");
    // line 1: "  input clk"
    CHECK(wordAtPosition(src, 1, 8)  == "clk");
    // line 2: ");"
    CHECK(wordAtPosition(src, 2, 0)  == "");
    // line 3: "endmodule"
    CHECK(wordAtPosition(src, 3, 0)  == "endmodule");
}

TEST_CASE("wordAtPosition: returns empty on out-of-range positions", "[symbol_utils]")
{
    CHECK(wordAtPosition("module", 1, 0) == ""); // line 1 doesn't exist
    CHECK(wordAtPosition("module", 0, 99) == ""); // character past EOL
}

TEST_CASE("wordAtPosition: handles identifiers with $ and _", "[symbol_utils]")
{
    const std::string src = "$display _my_var end";
    CHECK(wordAtPosition(src, 0, 0)  == "$display");
    CHECK(wordAtPosition(src, 0, 9)  == "_my_var");
}

TEST_CASE("wordAtPosition: cursor on whitespace finds word to the left", "[symbol_utils]")
{
    // Space between 'a' and 'b': cursor is directly after 'a', so 'a' is returned.
    // This is intentional — completion needs to find the partial word to the left
    // of the insertion point.
    CHECK(wordAtPosition("a b", 0, 1) == "a");
    // But a leading space has nothing to the left.
    CHECK(wordAtPosition(" b", 0, 0) == "");
}

// ---------------------------------------------------------------------------
// symbolKindFor
// ---------------------------------------------------------------------------

TEST_CASE("symbolKindFor: maps all known kinds", "[symbol_utils]")
{
    CHECK(symbolKindFor("Module")    == lsp::SymbolKind::Module);
    CHECK(symbolKindFor("Interface") == lsp::SymbolKind::Interface);
    CHECK(symbolKindFor("Package")   == lsp::SymbolKind::Package);
    CHECK(symbolKindFor("Class")     == lsp::SymbolKind::Class);
    CHECK(symbolKindFor("Function")  == lsp::SymbolKind::Function);
    CHECK(symbolKindFor("Task")      == lsp::SymbolKind::Method);
    CHECK(symbolKindFor("Port")      == lsp::SymbolKind::Field);
    CHECK(symbolKindFor("Signal")    == lsp::SymbolKind::Variable);
    CHECK(symbolKindFor("Parameter") == lsp::SymbolKind::Constant);
    CHECK(symbolKindFor("Macro")     == lsp::SymbolKind::Constant);
}

TEST_CASE("symbolKindFor: unknown kind falls back to Variable", "[symbol_utils]")
{
    CHECK(symbolKindFor("Unknown") == lsp::SymbolKind::Variable);
}

// ---------------------------------------------------------------------------
// makeRange
// ---------------------------------------------------------------------------

TEST_CASE("makeRange: converts 1-based line to 0-based LSP range", "[symbol_utils]")
{
    auto r = makeRange(4, 7, 5); // line 4 (1-based) col 7, name len 5
    CHECK(r.start.line      == 3u);
    CHECK(r.start.character == 7u);
    CHECK(r.end.line        == 3u);
    CHECK(r.end.character   == 12u);
}

// ---------------------------------------------------------------------------
// pickBestSymbol
// ---------------------------------------------------------------------------

namespace {
SymbolRow makeRow(std::string kind, std::string filePath)
{
    return {0, std::move(kind), "Foo", 1, 0, "", "", std::move(filePath), 0, ""};
}
} // namespace

TEST_CASE("pickBestSymbol: prefers a same-file match over anything else", "[symbol_utils]")
{
    std::vector<SymbolRow> rows{
        makeRow("Class",  "/a.sv"),
        makeRow("Signal", "/b.sv"),
    };
    const auto* best = pickBestSymbol(rows, "/b.sv");
    CHECK(best->filePath == "/b.sv");
    CHECK(best->kind == "Signal");
}

TEST_CASE("pickBestSymbol: no same-file match — prefers a declaration-like kind", "[symbol_utils]")
{
    // Simulates the real UVM bug: a corrupted Signal named "Foo" sorts before
    // the real Class "Foo" by path, but the Class should still win.
    std::vector<SymbolRow> rows{
        makeRow("Signal", "/aaa_corrupted.sv"),
        makeRow("Class",  "/zzz_real.sv"),
    };
    const auto* best = pickBestSymbol(rows, "/current.sv");
    CHECK(best->kind == "Class");
    CHECK(best->filePath == "/zzz_real.sv");
}

TEST_CASE("pickBestSymbol: no declaration-like kind present — falls back to first row", "[symbol_utils]")
{
    std::vector<SymbolRow> rows{
        makeRow("Signal", "/a.sv"),
        makeRow("Port",   "/b.sv"),
    };
    const auto* best = pickBestSymbol(rows, "/current.sv");
    CHECK(best == &rows.front());
}

TEST_CASE("pickBestSymbol: single row — returned regardless of kind", "[symbol_utils]")
{
    std::vector<SymbolRow> rows{makeRow("Signal", "/a.sv")};
    const auto* best = pickBestSymbol(rows, "/current.sv");
    CHECK(best == &rows.front());
}

// ---------------------------------------------------------------------------
// dotCompletionContext
// ---------------------------------------------------------------------------

TEST_CASE("dotCompletionContext: foo.b| yields a single segment 'foo', prefix=b", "[symbol_utils]")
{
    auto ctx = dotCompletionContext("foo.b", 0, 5);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "foo");
    CHECK_FALSE(ctx->segments[0].isCall);
    CHECK(ctx->prefix == "b");
}

TEST_CASE("dotCompletionContext: foo.| yields a single segment 'foo', empty prefix", "[symbol_utils]")
{
    auto ctx = dotCompletionContext("foo.", 0, 4);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "foo");
    CHECK(ctx->prefix.empty());
}

TEST_CASE("dotCompletionContext: foo| with no dot is not a dot-completion context", "[symbol_utils]")
{
    CHECK_FALSE(dotCompletionContext("foo", 0, 3).has_value());
}

TEST_CASE("dotCompletionContext: bare '.' with no preceding identifier is not a dot-completion context", "[symbol_utils]")
{
    CHECK_FALSE(dotCompletionContext(".", 0, 1).has_value());
}

TEST_CASE("dotCompletionContext: multi-line text resolves the dot on the requested line", "[symbol_utils]")
{
    const std::string src = "module m;\n  foo.ba\nendmodule";
    auto ctx = dotCompletionContext(src, 1, 7); // "  foo.ba" -> cursor after "ba"
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "foo");
    CHECK(ctx->prefix == "ba");
}

TEST_CASE("dotCompletionContext: out-of-range position returns nullopt", "[symbol_utils]")
{
    CHECK_FALSE(dotCompletionContext("foo.b", 1, 0).has_value()); // no line 1
    CHECK_FALSE(dotCompletionContext("foo.b", 0, 99).has_value()); // char past EOL
}

// ---------------------------------------------------------------------------
// dotCompletionContext -- chained/function-call segments (plan.md §6.14)
// ---------------------------------------------------------------------------

TEST_CASE("dotCompletionContext: foo.bar.b| yields two bare-identifier segments", "[symbol_utils][chain]")
{
    auto ctx = dotCompletionContext("foo.bar.b", 0, 9);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 2);
    CHECK(ctx->segments[0].name == "foo");
    CHECK_FALSE(ctx->segments[0].isCall);
    CHECK(ctx->segments[1].name == "bar");
    CHECK_FALSE(ctx->segments[1].isCall);
    CHECK(ctx->prefix == "b");
}

TEST_CASE("dotCompletionContext: a().b().c| yields two call segments", "[symbol_utils][chain]")
{
    auto ctx = dotCompletionContext("a().b().c", 0, 9);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 2);
    CHECK(ctx->segments[0].name == "a");
    CHECK(ctx->segments[0].isCall);
    CHECK(ctx->segments[1].name == "b");
    CHECK(ctx->segments[1].isCall);
    CHECK(ctx->prefix == "c");
}

TEST_CASE("dotCompletionContext: a call argument containing '.' doesn't confuse the chain",
          "[symbol_utils][chain]")
{
    // foo("a.b").c| -- the '.' inside the string must not be read as a
    // chain separator, and the ')' inside it must not end the call early.
    const std::string src = "foo(\"a.b\").c";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "foo");
    CHECK(ctx->segments[0].isCall);
    CHECK(ctx->prefix == "c");
}

TEST_CASE("dotCompletionContext: nested calls resolve the outer paren, not the inner one",
          "[symbol_utils][chain]")
{
    // a(b(c)).d| -- the outer call is "a", not "b" or "c".
    const std::string src = "a(b(c)).d";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "a");
    CHECK(ctx->segments[0].isCall);
    CHECK(ctx->prefix == "d");
}

TEST_CASE("dotCompletionContext: a three-level chain mixing calls and identifiers",
          "[symbol_utils][chain]")
{
    // make_factory().get_child().gr| -- Factory/Greeter shape from plan.md §6.14.
    const std::string src = "make_factory().get_child().gr";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 2);
    CHECK(ctx->segments[0].name == "make_factory");
    CHECK(ctx->segments[0].isCall);
    CHECK(ctx->segments[1].name == "get_child");
    CHECK(ctx->segments[1].isCall);
    CHECK(ctx->prefix == "gr");
}

TEST_CASE("dotCompletionContext: whitespace before a call's own paren is tolerated",
          "[symbol_utils][chain]")
{
    const std::string src = "foo ().c";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "foo");
    CHECK(ctx->segments[0].isCall);
}

TEST_CASE("dotCompletionContext: an unmatched call paren fails closed", "[symbol_utils][chain]")
{
    CHECK_FALSE(dotCompletionContext("foo).c", 0, 6).has_value());
}

// ---------------------------------------------------------------------------
// dotCompletionContext -- indexed access segments (plan.md §6.15)
// ---------------------------------------------------------------------------

TEST_CASE("dotCompletionContext: arr[i].m| yields one segment with indexDepth=1",
          "[symbol_utils][chain][index]")
{
    const std::string src = "arr[i].m";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "arr");
    CHECK_FALSE(ctx->segments[0].isCall);
    CHECK(ctx->segments[0].indexDepth == 1);
    CHECK(ctx->prefix == "m");
}

TEST_CASE("dotCompletionContext: arr[i][j].m| yields one segment with indexDepth=2 (not two segments)",
          "[symbol_utils][chain][index]")
{
    const std::string src = "arr[i][j].m";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "arr");
    CHECK(ctx->segments[0].indexDepth == 2);
}

TEST_CASE("dotCompletionContext: a bare identifier segment has indexDepth=0",
          "[symbol_utils][chain][index]")
{
    auto ctx = dotCompletionContext("foo.b", 0, 5);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].indexDepth == 0);
}

TEST_CASE("dotCompletionContext: a call segment has indexDepth=0",
          "[symbol_utils][chain][index]")
{
    auto ctx = dotCompletionContext("a().b", 0, 5);
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].isCall);
    CHECK(ctx->segments[0].indexDepth == 0);
}

TEST_CASE("dotCompletionContext: foo.arr[i].m| mixes a plain segment and an indexed one",
          "[symbol_utils][chain][index]")
{
    const std::string src = "foo.arr[i].m";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 2);
    CHECK(ctx->segments[0].name == "foo");
    CHECK(ctx->segments[0].indexDepth == 0);
    CHECK(ctx->segments[1].name == "arr");
    CHECK(ctx->segments[1].indexDepth == 1);
}

TEST_CASE("dotCompletionContext: an index expression containing '.' doesn't confuse the chain",
          "[symbol_utils][chain][index]")
{
    // aa["a.b"].m| -- the '.' inside the string literal key must not be
    // read as a chain separator.
    const std::string src = "aa[\"a.b\"].m";
    auto ctx = dotCompletionContext(src, 0, static_cast<unsigned>(src.size()));
    REQUIRE(ctx.has_value());
    REQUIRE(ctx->segments.size() == 1);
    CHECK(ctx->segments[0].name == "aa");
    CHECK(ctx->segments[0].indexDepth == 1);
    CHECK(ctx->prefix == "m");
}

TEST_CASE("dotCompletionContext: an unmatched index bracket fails closed",
          "[symbol_utils][chain][index]")
{
    CHECK_FALSE(dotCompletionContext("arr].c", 0, 6).has_value());
}

TEST_CASE("dotCompletionContext: '[...]' with no identifier before it fails closed",
          "[symbol_utils][chain][index]")
{
    CHECK_FALSE(dotCompletionContext("[i].c", 0, 5).has_value());
}
