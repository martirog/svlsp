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
