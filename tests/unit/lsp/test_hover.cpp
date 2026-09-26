#include <catch2/catch_test_macros.hpp>
#include "lsp/hover.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::HoverParams makeParams(std::string_view path, unsigned line, unsigned col) {
    lsp::HoverParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

} // namespace

TEST_CASE("HoverProvider: null when cursor is not on an identifier", "[hover]")
{
    Fixture f;
    // Space at position 0 in "  module"
    const std::string text = "  module top;";
    auto result = HoverProvider::getHover(makeParams("/t.sv", 0, 0), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("HoverProvider: null when identifier not in DB", "[hover]")
{
    Fixture f;
    const std::string text = "module adder;";
    // "adder" is at pos 7 but not in DB
    auto result = HoverProvider::getHover(makeParams("/t.sv", 0, 7), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("HoverProvider: returns markdown hover for a module", "[hover]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/adder.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "adder", 4, 7, "", "", 12, ""},
    });

    // "module adder #(" — cursor on "adder" at character 7
    const std::string text = "// line0\n// line1\n\nmodule adder #(";
    auto result = HoverProvider::getHover(makeParams("/adder.sv", 3, 7), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    // Contents should be MarkupContent
    REQUIRE(std::holds_alternative<lsp::MarkupContent>(result->contents));
    const auto& mc = std::get<lsp::MarkupContent>(result->contents);
    CHECK(std::string(mc.value).find("adder") != std::string::npos);
    CHECK(std::string(mc.value).find("Module") != std::string::npos);
}

TEST_CASE("HoverProvider: hover includes scope for nested symbols", "[hover]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Signal, "clk", 5, 4, "top", "", 0, "top"},
    });

    const std::string text = "// 0\n// 1\n// 2\n// 3\n    clk;";
    // line 4 (0-based), character 4
    auto result = HoverProvider::getHover(makeParams("/t.sv", 4, 4), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& mc = std::get<lsp::MarkupContent>(result->contents);
    const std::string val{mc.value};
    CHECK(val.find("clk")    != std::string::npos);
    CHECK(val.find("Signal") != std::string::npos);
    CHECK(val.find("top")    != std::string::npos);
}

TEST_CASE("HoverProvider: hover includes detail for function return type", "[hover]")
{
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "calc", 2, 9, "top", "logic", 8, "top"},
    });

    // line 1 (0-based): "  function calc"
    const std::string text = "module top;\n  function calc";
    auto result = HoverProvider::getHover(makeParams("/t.sv", 1, 11), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("logic")    != std::string::npos);
    CHECK(val.find("Function") != std::string::npos);
}

TEST_CASE("HoverProvider: prefers a declaration-like kind over an alphabetically-earlier data-like one", "[hover]")
{
    Fixture f;
    // "/a_signal.sv" sorts before "/z_class.sv" — without the kind-preference
    // tiebreak in pickBestSymbol(), the plain ORDER BY path, line in
    // findSymbolsByName would pick the Signal here.
    f.sdb.replaceSymbols(f.sdb.upsertFile("/a_signal.sv", "h"),
        {{ParseRecordKind::Signal, "Widget", 3, 2, "", "", 0, ""}});
    f.sdb.replaceSymbols(f.sdb.upsertFile("/z_class.sv",  "h"),
        {{ParseRecordKind::Class, "Widget", 5, 6, "", "", 20, ""}});

    // Requested from neither candidate file.
    const std::string text = "Widget obj;";
    auto result = HoverProvider::getHover(makeParams("/user.sv", 0, 0), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("Class")  != std::string::npos);
    CHECK(val.find("Signal") == std::string::npos);
}

TEST_CASE("HoverProvider: a pkg::-qualified name shows the named package's symbol, not a "
          "same-named decoy",
          "[hover][scoped]")
{
    // Two packages in /pkgs.sv each declare a class `Item`; a name-only
    // lookup picks pkg_a's (earlier line). `pkg_b::Item` must show pkg_b's.
    Fixture f;
    int64_t pk = f.sdb.upsertFile("/pkgs.sv", "h");
    f.sdb.replaceSymbols(pk, {
        {ParseRecordKind::Package, "pkg_a", 1, 8, "", "", 3, ""},
        {ParseRecordKind::Class,   "Item",  2, 8, "pkg_a", "", 2, "pkg_a"},
        {ParseRecordKind::Package, "pkg_b", 4, 8, "", "", 6, ""},
        {ParseRecordKind::Class,   "Item",  5, 8, "pkg_b", "", 5, "pkg_b"},
    });
    int64_t top = f.sdb.upsertFile("/top.sv", "h");
    f.sdb.replaceSymbols(top, {
        {ParseRecordKind::Module, "top", 1, 7, "", "", 3, ""},
    });

    const std::string text = "module top;\n  pkg_b::Item x;\nendmodule\n";
    auto result = HoverProvider::getHover(makeParams("/top.sv", 1, 10), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    const std::string val{std::get<lsp::MarkupContent>(result->contents).value};
    CHECK(val.find("pkg_b") != std::string::npos);
    CHECK(val.find("pkg_a") == std::string::npos);
    CHECK(result->range->start.line == 4);
}

TEST_CASE("HoverProvider: null for a cursor inside a comment or string literal",
          "[hover][scoped]")
{
    // A name mentioned in `// put` or "put" is not a use of anything
    // (handoff open-work item 1: this used to hover an unrelated `put`).
    Fixture f;
    int64_t fid = f.sdb.upsertFile("/c.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "put", 1, 16, "", "", 2, ""},
    });

    const std::string text =
        "function void put();\n"
        "endfunction // put\n"
        "/* put */ string s = \"put\";\n";
    CHECK(HoverProvider::getHover(makeParams("/c.sv", 1, 16), f.sdb, text).isNull());
    CHECK(HoverProvider::getHover(makeParams("/c.sv", 2, 4), f.sdb, text).isNull());
    CHECK(HoverProvider::getHover(makeParams("/c.sv", 2, 23), f.sdb, text).isNull());
    // ...while the real declaration still hovers.
    CHECK_FALSE(HoverProvider::getHover(makeParams("/c.sv", 0, 15), f.sdb, text).isNull());
}

// ---------------------------------------------------------------------------
// Macros (plan.md §6.29 follow-up): hover on a `NAME use or a `define name
// ---------------------------------------------------------------------------

TEST_CASE("HoverProvider: a function-like macro shows its signature and body",
          "[hover][macro][phase6.29]")
{
    Fixture f;
    f.sdb.replaceMacros(f.sdb.upsertFile("/m.svh", "h"),
                        {MacroRecord{"LOG", "$display(ID, MSG)", 3, 8, "", true, {"ID", "MSG"},
                                     {std::nullopt, std::string("\"\"")}}});
    const std::string text = "initial `LOG(\"a\", \"b\");";
    auto result = HoverProvider::getHover(makeParams("/t.sv", 0, 10), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    const auto& mc = std::get<lsp::MarkupContent>(result.value().contents);
    const std::string v{mc.value};
    CHECK(v.find("**Macro**") != std::string::npos);
    CHECK(v.find("`LOG(ID, MSG = \"\")") != std::string::npos);
    CHECK(v.find("$display(ID, MSG)") != std::string::npos);
    CHECK(v.find("/m.svh:3") != std::string::npos);
    // The range is the name under the cursor, not the definition.
    REQUIRE(result.value().range.has_value());
    CHECK(result.value().range->start.line == 0);
    CHECK(result.value().range->start.character == 9);
    CHECK(result.value().range->end.character == 12);
}

TEST_CASE("HoverProvider: an object-like macro shows its value, a long body is cut",
          "[hover][macro][phase6.29]")
{
    Fixture f;
    f.sdb.replaceMacros(f.sdb.upsertFile("/m.svh", "h"),
                        {MacroRecord{"W", "8", 1, 8, "", false, {}, {}},
                         MacroRecord{"BIG", std::string(2000, 'x'), 2, 8, "", false, {}, {}}});
    auto w = HoverProvider::getHover(makeParams("/t.sv", 0, 8), f.sdb, "logic [`W-1:0] d;");
    REQUIRE_FALSE(w.isNull());
    const std::string wv{std::get<lsp::MarkupContent>(w.value().contents).value};
    CHECK(wv.find("`W") != std::string::npos);
    CHECK(wv.find("```systemverilog\n8\n```") != std::string::npos);

    auto big = HoverProvider::getHover(makeParams("/t.sv", 0, 2), f.sdb, "`BIG");
    REQUIRE_FALSE(big.isNull());
    const std::string bv{std::get<lsp::MarkupContent>(big.value().contents).value};
    CHECK(bv.size() < 1000);
    CHECK(bv.find("…") != std::string::npos);
}

TEST_CASE("HoverProvider: a macro and a same-named parameter each hover as themselves",
          "[hover][macro][phase6.29]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "hm", 1, 7, "", "", 4, ""},
        {ParseRecordKind::Parameter, "WIDTH", 2, 13, "hm", "int", 0, "hm"},
    });
    f.sdb.replaceMacros(fid, {MacroRecord{"WIDTH", "16", 1, 8, "", false, {}, {}}});
    const std::string text =
        "module hm;\n"
        "  parameter WIDTH = 4;\n"
        "  logic [`WIDTH-1:0] a, b [WIDTH];\n"
        "endmodule\n";
    auto mac = HoverProvider::getHover(makeParams("/t.sv", 2, 10), f.sdb, text);
    REQUIRE_FALSE(mac.isNull());
    CHECK(std::string(std::get<lsp::MarkupContent>(mac.value().contents).value)
              .find("**Macro**") != std::string::npos);
    auto par = HoverProvider::getHover(makeParams("/t.sv", 2, 27), f.sdb, text);
    REQUIRE_FALSE(par.isNull());
    CHECK(std::string(std::get<lsp::MarkupContent>(par.value().contents).value)
              .find("**Parameter**") != std::string::npos);
}

TEST_CASE("HoverProvider: an unknown macro is null, never a same-named symbol",
          "[hover][macro][phase6.29]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {{ParseRecordKind::Module, "NOPE", 1, 7, "", "", 2, ""}});
    CHECK(HoverProvider::getHover(makeParams("/t.sv", 0, 2), f.sdb, "`NOPE").isNull());
}
