#include <catch2/catch_test_macros.hpp>
#include "lsp/signature_help.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "compiler/parse_record.h"
#include "lsp/sv_system_tasks.h"

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

// ---------------------------------------------------------------------------
// Module/interface/program port type information (plan.md §6.28) -- proves
// portLabel() needs no changes: the type enterAnsi_port_declaration now
// records in `detail` renders through the same "<prefix> <name>[<suffix>]"
// path function/task parameters already use.
// ---------------------------------------------------------------------------

TEST_CASE("SignatureHelpProvider: a typed module port shows its type in the label",
          "[signature_help][phase6.28]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/wide.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "wide", 1, 7, "", "", 3, ""},
        {ParseRecordKind::Port, "width", 2, 2, "wide", "input int", 0, "wide"},
        {ParseRecordKind::Port, "valid", 3, 2, "wide", "output bit", 0, "wide"},
    });
    const std::string text = "wide u1(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& params_ = *result.value().signatures[0].parameters;
    REQUIRE(params_.size() == 2);
    CHECK(std::get<std::string>(params_[0].label) == "input int width");
    CHECK(std::get<std::string>(params_[1].label) == "output bit valid");
}

TEST_CASE("SignatureHelpProvider: a module port's default value renders after the name",
          "[signature_help][phase6.28]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/w.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "w", 1, 7, "", "", 2, ""},
        {ParseRecordKind::Port, "width", 2, 2, "w", std::string("input int") +
            PARAM_DEFAULT_VALUE_SEP + " = 8", 0, "w"},
    });
    const std::string text = "w u1(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& params_ = *result.value().signatures[0].parameters;
    REQUIRE(params_.size() == 1);
    CHECK(std::get<std::string>(params_[0].label) == "input int width = 8");
}

TEST_CASE("SignatureHelpProvider: an untyped module port falls back to direction only",
          "[signature_help][phase6.28]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/plain.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Module, "plain", 1, 7, "", "", 2, ""},
        {ParseRecordKind::Port, "clk", 2, 2, "plain", "input", 0, "plain"},
    });
    const std::string text = "plain u1(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());

    const auto& params_ = *result.value().signatures[0].parameters;
    REQUIRE(params_.size() == 1);
    CHECK(std::get<std::string>(params_[0].label) == "input clk");
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

TEST_CASE("SignatureHelpProvider: null for a dotted call whose receiver is undeclared",
          "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Function, "get_val", 1, 13, "", "int", 0, ""},
        {ParseRecordKind::Port, "x", 1, 20, "get_val", "int", 0, "get_val"},
    });
    // `obj` itself has no Signal row anywhere -- the receiver chain fails to
    // resolve (fail closed), same posture as every other resolution failure
    // in this file. Since plan.md §6.27, a dotted call with a *resolvable*
    // receiver IS supported -- see the tests below.
    const std::string text = "y = obj.get_val(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE(result.isNull());
}

TEST_CASE("SignatureHelpProvider: resolves a dotted call to the receiver's own declared method "
          "(plan.md §6.27)", "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Signal,   "obj",     1,  0, "",       "Widget", 0,  ""},
        {ParseRecordKind::Class,    "Widget",  10, 6, "",       "",       12, ""},
        {ParseRecordKind::Function, "get_val", 11, 6, "Widget", "int",    0,  "Widget"},
        {ParseRecordKind::Port,     "idx",     11, 20, "get_val", "int",  0,  "Widget::get_val"},
    });
    const std::string text = "obj.get_val(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].parameters->size() == 1);
    CHECK(result.value().signatures[0].label == "get_val(int idx)");
}

TEST_CASE("SignatureHelpProvider: resolves a dotted call to a method inherited from an ancestor "
          "class (plan.md §6.27)", "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Signal,   "obj",   1,  0, "",      "Child", 0,  ""},
        {ParseRecordKind::Class,    "Base",  10, 6, "",      "",      12, ""},
        {ParseRecordKind::Function, "greet", 11, 6, "Base",  "void",  0,  "Base"},
        {ParseRecordKind::Class,    "Child", 20, 6, "",      "Base",  22, ""},
    });
    const std::string text = "obj.greet(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().signatures[0].parameters.has_value());
    CHECK(result.value().signatures[0].parameters->empty());
}

TEST_CASE("SignatureHelpProvider: resolves a two-segment dotted chain (obj.field.method()) "
          "(plan.md §6.27)", "[signature_help]")
{
    Fixture f;
    auto fid = f.sdb.upsertFile("/t.sv", "h");
    f.sdb.replaceSymbols(fid, {
        {ParseRecordKind::Signal,   "obj",     1,  0, "",       "Outer",  0,  ""},
        {ParseRecordKind::Class,    "Outer",   10, 6, "",       "",       12, ""},
        {ParseRecordKind::Signal,   "field",   11, 6, "Outer",  "Widget", 0,  "Outer"},
        {ParseRecordKind::Class,    "Widget",  20, 6, "",       "",       22, ""},
        {ParseRecordKind::Function, "get_val", 21, 6, "Widget", "int",    0,  "Widget"},
    });
    const std::string text = "obj.field.get_val(";
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), f.sdb, text);
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].label == "get_val()");
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

// ---------------------------------------------------------------------------
// Dotted call through a package-scoped variable, driven through a real
// compile pipeline (not hand-built SymbolRows) so real `import` statements,
// real `::`-qualified variable declarations, and a real `extends` chain are
// all exercised together (plan.md §6.27). Every case below deliberately
// declares its variable with the fully package-qualified type
// (`pkg_a::ClassB a;`) *and* an `import` on its own separate line -- the
// import is never actually load-bearing for this resolution (baseClassChain/
// resolveMethod resolve a bare class name across the whole database
// regardless of import visibility, same as the qualifiedClassScope logic
// they replaced), but real UVM-style code writes both together, and this
// proves the combination parses and resolves correctly end to end rather
// than assuming it from the import-free unit tests above alone.
// ---------------------------------------------------------------------------

namespace {
struct RealCompileFixture {
    Database              db{":memory:"};
    SymbolDatabase         sdb{db};
    CompilationController  ctrl{sdb};
    RealCompileFixture() { db.initSchema(); }
};
} // namespace

TEST_CASE("SignatureHelpProvider: dotted call to a class method resolves through a specific "
          "import on a separate line (plan.md §6.27)", "[signature_help][real-compile]")
{
    RealCompileFixture f;
    const std::string source =
        "package pkg_a;\n"
        "  class ClassB;\n"
        "    function int get_something(int b, int c);\n"
        "      get_something = b + c;\n"
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n"
        "\n"
        "import pkg_a::ClassB;\n"
        "\n"
        "module top;\n"
        "  pkg_a::ClassB a;\n"
        "  initial begin\n"
        "    a.get_something(1, 2);\n" // line 13 (0-based)
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", source);

    // Cursor right after "get_something(" on line 13: 4 leading spaces +
    // "a.get_something(" (17 chars) = column 21... counted directly against
    // the line's own text below rather than by hand.
    const std::string line13 = "    a.get_something(1, 2);";
    const unsigned col = static_cast<unsigned>(line13.find('(')) + 1;
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 13, col), f.sdb, source);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().signatures[0].parameters.has_value());
    CHECK(result.value().signatures[0].parameters->size() == 2);
    CHECK(result.value().signatures[0].label == "get_something(int b, int c)");
}

TEST_CASE("SignatureHelpProvider: dotted call to a class method resolves through a wildcard "
          "import on a separate line (plan.md §6.27)", "[signature_help][real-compile]")
{
    RealCompileFixture f;
    const std::string source =
        "package pkg_a;\n"
        "  class ClassB;\n"
        "    function int get_something(int b, int c);\n"
        "      get_something = b + c;\n"
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n"
        "\n"
        "import pkg_a::*;\n"
        "\n"
        "module top;\n"
        "  pkg_a::ClassB a;\n"
        "  initial begin\n"
        "    a.get_something(1, 2);\n" // line 13 (0-based)
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", source);

    const std::string line13 = "    a.get_something(1, 2);";
    const unsigned col = static_cast<unsigned>(line13.find('(')) + 1;
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 13, col), f.sdb, source);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().signatures[0].parameters.has_value());
    CHECK(result.value().signatures[0].parameters->size() == 2);
    CHECK(result.value().signatures[0].label == "get_something(int b, int c)");
}

TEST_CASE("SignatureHelpProvider: dotted call resolves to a method declared on a class in a "
          "*different* package, imported by the variable's own declaring package, not the "
          "package the variable's class was imported from (plan.md §6.27)",
          "[signature_help][real-compile]")
{
    RealCompileFixture f;
    const std::string source =
        "package pkg_base;\n"
        "  class BaseClass;\n"
        "    function int get_something(int b, int c);\n"
        "      get_something = b + c;\n"
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n"
        "\n"
        "package pkg_a;\n"
        "  import pkg_base::*;\n"
        "  class ClassB extends BaseClass;\n"
        "  endclass\n"
        "endpackage\n"
        "\n"
        "import pkg_a::ClassB;\n"
        "\n"
        "module top;\n"
        "  pkg_a::ClassB a;\n"
        "  initial begin\n"
        "    a.get_something(1, 2);\n" // line 19 (0-based)
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", source);

    const std::string line19 = "    a.get_something(1, 2);";
    const unsigned col = static_cast<unsigned>(line19.find('(')) + 1;
    auto result = SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 19, col), f.sdb, source);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().signatures[0].parameters.has_value());
    CHECK(result.value().signatures[0].parameters->size() == 2);
    CHECK(result.value().signatures[0].label == "get_something(int b, int c)");
}

TEST_CASE("SignatureHelpProvider: inside an out-of-class body, a dotted call through a class "
          "field resolves, and an extern method's parameters are listed once (plan.md §6.30 "
          "step D)", "[signature_help][real-compile][outofclass]")
{
    // The UVM shape from plan.md §6.27: uvm_component::set_domain's
    // out-of-class body calls m_children[c].set_domain(...), where
    // m_children is a field of the class. The prototype and the body both
    // declare the parameters in the same scope; they must not be doubled.
    RealCompileFixture f;
    const std::string source =
        "package sd_p;\n"
        "  class SdComp;\n"
        "    SdComp m_children[string];\n"
        "    extern function void set_domain(int domain, int hier = 1);\n"
        "  endclass\n"
        "  function void SdComp::set_domain(int domain, int hier);\n"
        "    foreach (m_children[c])\n"
        "      m_children[c].set_domain(domain);\n" // line 7 (0-based)
        "  endfunction\n"
        "endpackage\n";
    f.ctrl.compile("/sd.sv", source);

    const std::string line7 = "      m_children[c].set_domain(domain);";
    const unsigned col = static_cast<unsigned>(line7.find('(')) + 1;
    auto result = SignatureHelpProvider::getSignatureHelp(makeParams("/sd.sv", 7, col), f.sdb,
                                                          source);
    REQUIRE_FALSE(result.isNull());
    REQUIRE(result.value().signatures[0].parameters.has_value());
    CHECK(result.value().signatures[0].parameters->size() == 2);
    CHECK(result.value().signatures[0].label == "set_domain(int domain, int hier = 1)");
}

// ---------------------------------------------------------------------------
// plan.md §6.29 part C: system tasks/functions from sv_system_tasks.h.
// ---------------------------------------------------------------------------

namespace {

lsp::TextDocument_SignatureHelpResult helpAtEnd(SymbolDatabase& sdb, const std::string& text)
{
    return SignatureHelpProvider::getSignatureHelp(
        makeParams("/t.sv", 0, static_cast<unsigned>(text.size())), sdb, text);
}

} // namespace

TEST_CASE("SignatureHelpProvider: system function with fixed parameters",
          "[signature_help][phase6.29]")
{
    Fixture f;
    auto result = helpAtEnd(f.sdb, "x = $urandom_range(10, ");
    REQUIRE_FALSE(result.isNull());
    const auto& sig = result.value().signatures[0];
    CHECK(sig.label == "$urandom_range(maxval, [minval])");
    REQUIRE(sig.parameters.has_value());
    CHECK(sig.parameters->size() == 2);
    REQUIRE(sig.activeParameter.has_value());
    CHECK(*sig.activeParameter == 1);
    CHECK(sig.documentation.has_value());
}

TEST_CASE("SignatureHelpProvider: $display clamps activeParameter to its variadic tail",
          "[signature_help][phase6.29]")
{
    Fixture f;
    auto result = helpAtEnd(f.sdb, "initial $display(\"%0d %0d\", a, ");
    REQUIRE_FALSE(result.isNull());
    const auto& sig = result.value().signatures[0];
    CHECK(sig.label == "$display(args...)");
    REQUIRE(sig.activeParameter.has_value());
    CHECK(*sig.activeParameter == 0);

    auto sf = helpAtEnd(f.sdb, "s = $sformatf(\"%0d %0d\", a, ");
    REQUIRE_FALSE(sf.isNull());
    const auto& sfSig = sf.value().signatures[0];
    CHECK(sfSig.label == "$sformatf(format, args...)");
    REQUIRE(sfSig.activeParameter.has_value());
    CHECK(*sfSig.activeParameter == 1);
}

TEST_CASE("SignatureHelpProvider: $fatal's optional leading finish_number",
          "[signature_help][phase6.29]")
{
    Fixture f;
    // With finish_number given, positions map directly.
    auto withNum = helpAtEnd(f.sdb, "$fatal(1, ");
    REQUIRE_FALSE(withNum.isNull());
    CHECK(withNum.value().signatures[0].label == "$fatal([finish_number], format, args...)");
    CHECK(withNum.value().signatures[0].activeParameter.value_or(99) == 1);

    // A leading string literal is the format: finish_number was omitted.
    auto noNum = helpAtEnd(f.sdb, "$fatal(\"bad\"");
    REQUIRE_FALSE(noNum.isNull());
    CHECK(noNum.value().signatures[0].activeParameter.value_or(99) == 1);
    auto noNumArgs = helpAtEnd(f.sdb, "$fatal(\"bad %0d\", x");
    REQUIRE_FALSE(noNumArgs.isNull());
    CHECK(noNumArgs.value().signatures[0].activeParameter.value_or(99) == 2);
}

TEST_CASE("SignatureHelpProvider: no-argument system function and unknown $task",
          "[signature_help][phase6.29]")
{
    Fixture f;
    auto t = helpAtEnd(f.sdb, "t = $time(");
    REQUIRE_FALSE(t.isNull());
    CHECK(t.value().signatures[0].label == "$time()");
    CHECK(t.value().signatures[0].parameters->empty());
    CHECK_FALSE(t.value().signatures[0].activeParameter.has_value());

    // A vendor/PLI task not in the table stays null.
    CHECK(helpAtEnd(f.sdb, "$vendor_task(").isNull());
    // A qualified name is never a system task.
    CHECK(helpAtEnd(f.sdb, "pkg::$display(").isNull());
}

TEST_CASE("SignatureHelpProvider: a nested system call inside another call's arguments",
          "[signature_help][phase6.29]")
{
    Fixture f;
    auto result = helpAtEnd(f.sdb, "$display(\"%0d\", $clog2(");
    REQUIRE_FALSE(result.isNull());
    CHECK(result.value().signatures[0].label == "$clog2(n)");
}

TEST_CASE("SignatureHelpProvider: every system task table entry is well formed",
          "[signature_help][phase6.29]")
{
    for (const auto& t : SYSTEM_TASKS) {
        INFO(t.name);
        CHECK(t.name.front() == '$');
        CHECK_FALSE(t.doc.empty());
        CHECK(findSystemTask(t.name) == &t); // no duplicate names
        const auto params = systemTaskParams(t);
        for (std::size_t i = 0; i < params.size(); ++i) {
            CHECK_FALSE(params[i].empty());
            // A variadic tail is always last.
            if (params[i].ends_with("...") || params[i].ends_with("...]"))
                CHECK(i + 1 == params.size());
        }
    }
}
