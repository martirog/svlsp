#include <catch2/catch_test_macros.hpp>
#include "lsp/signature_help.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"

namespace {

struct Fixture {
    Database       db{":memory:"};
    SymbolDatabase sdb{db};

    Fixture() { db.initSchema(); }
};

lsp::SignatureHelpParams makeParams(std::string_view path, unsigned line, unsigned col)
{
    lsp::SignatureHelpParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// Declares module `adder` with three ports (in a real project this comes
// from enterAnsi_port_declaration; inserted directly here for a fast,
// focused unit test).
void declareAdder(SymbolDatabase& sdb)
{
    auto fid = sdb.upsertFile("/adder.sv", "h");
    sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "adder", 1, 7, "", "", 5, ""},
        {ParseRecordKind::Port, "a",   2, 2, "adder", "input",  0, "adder"},
        {ParseRecordKind::Port, "b",   3, 2, "adder", "input",  0, "adder"},
        {ParseRecordKind::Port, "sum", 4, 2, "adder", "output", 0, "adder"},
    });
}

} // namespace

TEST_CASE("SignatureHelpProvider: null when cursor is not inside any parentheses",
          "[signature_help]")
{
    Fixture f;
    declareAdder(f.sdb);
    const std::string text = "adder u1();";
    auto result = SignatureHelpProvider::getSignatureHelp(makeParams("/t.sv", 0, 0), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("SignatureHelpProvider: null when the type name before '(' isn't a known "
          "module/interface/program", "[signature_help]")
{
    Fixture f;
    const std::string text = "not_a_module u1(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("SignatureHelpProvider: returns the port list for a module instantiation",
          "[signature_help]")
{
    Fixture f;
    declareAdder(f.sdb);
    const std::string text = "adder u1(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& help = result.value();
    REQUIRE(help.signatures.size() == 1);
    REQUIRE(help.signatures[0].parameters.has_value());
    CHECK(help.signatures[0].parameters->size() == 3);
    CHECK(help.signatures[0].label.find("adder(") == 0);
}

TEST_CASE("SignatureHelpProvider: active parameter tracks positional comma count",
          "[signature_help]")
{
    Fixture f;
    declareAdder(f.sdb);
    // Cursor right after the second comma -> third positional argument ("sum").
    const std::string text = "adder u1(x, y, ";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& sig = result.value().signatures[0];
    REQUIRE(sig.activeParameter.has_value());
    CHECK(*sig.activeParameter == 2);
}

TEST_CASE("SignatureHelpProvider: active parameter resolves a named port connection",
          "[signature_help]")
{
    Fixture f;
    declareAdder(f.sdb);
    // Named connection to the first-declared port ("a") but positioned as
    // if it were the second argument -- the named lookup must win over the
    // positional count.
    const std::string text = "adder u1(.b(x), .a(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& sig = result.value().signatures[0];
    REQUIRE(sig.activeParameter.has_value());
    CHECK(*sig.activeParameter == 0); // "a" is ports[0]
}

TEST_CASE("SignatureHelpProvider: works for an interface instantiation too",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/bus_if.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Interface, "bus_if", 1, 10, "", "", 3, ""},
        {ParseRecordKind::Port, "clk", 2, 2, "bus_if", "input", 0, "bus_if"},
    });
    const std::string text = "bus_if u_bus(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].parameters->size() == 1);
}

TEST_CASE("SignatureHelpProvider: null for a function call (no per-parameter data tracked)",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "my_func", 1, 13, "", "int", 0, ""},
    });
    const std::string text = "x = my_func(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE(result.isNull()); // disclosed scope limitation -- see signature_help.h
}
