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

TEST_CASE("SignatureHelpProvider: null for a call to something that isn't a function/task/design-unit",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Signal, "not_callable", 1, 4, "", "int", 0, ""},
    });
    const std::string text = "x = not_callable(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("SignatureHelpProvider: null for a dotted call (out of scope -- needs chain resolution)",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "get_val", 1, 13, "", "int", 0, ""},
        {ParseRecordKind::Port, "x", 1, 20, "get_val", "int", 0, "get_val"},
    });
    const std::string text = "y = obj.get_val(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE(result.isNull()); // disclosed scope limitation -- see signature_help.h
}

// Declares function `my_func` (int a, output int b) -- as a real compile
// would produce via enterTf_port_item (sv_tree_walker.cpp): two Port rows
// scoped to "my_func".
void declareMyFunc(SymbolDatabase& sdb)
{
    auto fid = sdb.upsertFile("/t.sv", "h");
    sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "my_func", 1, 13, "", "int", 0, ""},
        {ParseRecordKind::Port, "a", 1, 25, "my_func", "int",         0, "my_func"},
        {ParseRecordKind::Port, "b", 1, 40, "my_func", "output int",  0, "my_func"},
    });
}

TEST_CASE("SignatureHelpProvider: returns the parameter list for a bare function call",
          "[signature_help]")
{
    Fixture f;
    declareMyFunc(f.sdb);
    const std::string text = "x = my_func(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& help = result.value();
    REQUIRE(help.signatures.size() == 1);
    REQUIRE(help.signatures[0].parameters.has_value());
    CHECK(help.signatures[0].parameters->size() == 2);
    CHECK(help.signatures[0].label == "my_func(int a, output int b)");
}

TEST_CASE("SignatureHelpProvider: works for a task call too", "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Task, "my_task", 1, 5, "", "", 0, ""},
        {ParseRecordKind::Port, "x", 1, 18, "my_task", "int", 0, "my_task"},
    });
    const std::string text = "my_task(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].parameters->size() == 1);
}

TEST_CASE("SignatureHelpProvider: a class method called bare from inside its own class resolves",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Class, "MyClass", 1, 6, "", "", 5, ""},
        {ParseRecordKind::Function, "get_val", 2, 6, "MyClass", "int", 0, "MyClass"},
        {ParseRecordKind::Port, "idx", 2, 18, "get_val", "int", 0, "MyClass::get_val"},
    });
    const std::string text = "return get_val(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].parameters->size() == 1);
}

TEST_CASE("SignatureHelpProvider: a Class::-qualified bare call resolves to that exact "
          "class's own method, not an unrelated same-named method elsewhere (plan.md §6.26)",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Class,    "Other",  1, 6, "",      "",     3, ""},
        {ParseRecordKind::Function, "get",    2, 6, "Other", "int",  0, "Other"},
        {ParseRecordKind::Port,     "key",    2, 18, "get",  "int",  0, "Other::get"},
        {ParseRecordKind::Class,    "type_id", 10, 6, "",     "",    12, ""},
        {ParseRecordKind::Function, "get",    11, 6, "type_id", "int", 0, "type_id"},
    });
    // If this fell back to a flat whole-database search (the pre-§6.26
    // behavior), it could just as easily resolve against `Other::get`
    // (1 parameter) instead of `type_id::get` (0 parameters) -- the exact
    // shape of the UVM-corpus `type_id::get()` false positive.
    const std::string text = "x = type_id::get(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().signatures[0].parameters.has_value());
    CHECK(result.value().signatures[0].parameters->empty());
}

TEST_CASE("SignatureHelpProvider: zero-argument call resolves to an empty parameter list, not null",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "no_args", 1, 13, "", "int", 0, ""},
    });
    const std::string text = "x = no_args(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    const auto& help = result.value();
    REQUIRE(help.signatures[0].parameters.has_value());
    CHECK(help.signatures[0].parameters->empty());
    CHECK(help.signatures[0].label == "no_args()");
}

TEST_CASE("SignatureHelpProvider: a default parameter value is rendered after the name",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "with_default", 1, 13, "", "int", 0, ""},
        {ParseRecordKind::Port, "width", 1, 25, "with_default", std::string("int") + PARAM_DEFAULT_VALUE_SEP + " = 8",
         0, "with_default"},
    });
    const std::string text = "x = with_default(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].label == "with_default(int width = 8)");
}
