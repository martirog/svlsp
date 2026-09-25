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
