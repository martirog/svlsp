// resolveSymbolAt (plan.md §6.30 step A) -- the shared, scope-aware
// resolver hover/definition/references/rename switch to in step B. Driven
// through a real CompilationController::compile. Every case has a
// same-named decoy a name-only lookup would pick, and asserts the exact
// declaration (file, line, column) plus whether resolution was exact or the
// name-only fallback.
//
// Fixture convention: `// @name` markers inside the SV text; posOf(text,
// "@name", "token") is the first occurrence of `token` on that line.

#include <catch2/catch_test_macros.hpp>
#include "lsp/symbol_resolution.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {

struct F {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController ctrl{sdb};
    std::map<std::string, std::string> files;

    F() { db.initSchema(); }

    void add(const std::string& path, const std::string& text)
    {
        files[path] = text;
        ctrl.compile(path, text);
    }
};

struct Pos { unsigned line; unsigned col; };

Pos posOf(const std::string& text, const std::string& marker, const std::string& token)
{
    unsigned line = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string l = text.substr(start, end - start);
        if (l.find(marker) != std::string::npos) {
            const size_t c = l.find(token);
            REQUIRE(c != std::string::npos);
            return {line, static_cast<unsigned>(c)};
        }
        start = end + 1;
        ++line;
    }
    FAIL("marker not found: " << marker);
    return {0, 0};
}

std::optional<ResolvedSymbol> resolveAt(F& f, const std::string& path, Pos p)
{
    return resolveSymbolAt(f.sdb, path, f.files.at(path), p.line, p.col);
}

// Resolving `useToken` on the `useMarker` line of `usePath` lands exactly on
// `declToken` on the `declMarker` line of `declPath`. A token may carry
// trailing context to disambiguate its position (`stat(`, `n)`); only its
// leading identifier is compared against the resolved name.
void requireResolvesTo(F& f, const std::string& usePath, const std::string& useMarker,
                       const std::string& useToken, const std::string& declPath,
                       const std::string& declMarker, const std::string& declToken,
                       bool exact = true)
{
    auto r = resolveAt(f, usePath, posOf(f.files.at(usePath), useMarker, useToken));
    REQUIRE(r.has_value());
    const Pos want = posOf(f.files.at(declPath), declMarker, declToken);
    size_t n = 0;
    while (n < declToken.size() && (std::isalnum(static_cast<unsigned char>(declToken[n])) ||
                                    declToken[n] == '_'))
        ++n;
    CHECK(r->row.filePath == declPath);
    CHECK(r->row.name == declToken.substr(0, n));
    CHECK(r->row.line == static_cast<int>(want.line) + 1);
    CHECK(r->row.col == static_cast<int>(want.col));
    CHECK(r->exact == exact);
}

void requireResolves(F& f, const std::string& usePath, const std::string& useMarker,
                     const std::string& token, const std::string& declPath,
                     const std::string& declMarker, bool exact = true)
{
    requireResolvesTo(f, usePath, useMarker, token, declPath, declMarker, token, exact);
}

std::string writeTemp(const std::string& name, const std::string& content)
{
    const auto dir = std::filesystem::temp_directory_path() / "svlsp_test_resolution";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::ofstream(path) << content;
    return path;
}

const std::string kPkgs =
    "package pkg_a; // @a_pkg\n"
    "  class Item; // @a_item\n"
    "    int val; // @a_val\n"
    "    function int get(); return 1; endfunction // @a_get\n"
    "    static function int stat(); return 1; endfunction // @a_stat\n"
    "  endclass\n"
    "  function int make(); return 1; endfunction // @a_make\n"
    "endpackage\n"
    "package pkg_b; // @b_pkg\n"
    "  class Item; // @b_item\n"
    "    int val; // @b_val\n"
    "    function int get(); return 2; endfunction // @b_get\n"
    "    static function int stat(); return 2; endfunction // @b_stat\n"
    "  endclass\n"
    "  function int make(); return 2; endfunction // @b_make\n"
    "endpackage\n";

} // namespace

// ---------------------------------------------------------------------------
// :: qualification
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: pkg::Class picks the named package's class",
          "[resolution][scope-op]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/top.sv",
          "module top;\n"
          "  pkg_a::Item a; // @ua\n"
          "  pkg_b::Item b; // @ub\n"
          "endmodule\n");
    requireResolves(f, "/top.sv", "@ua", "Item", "/pkgs.sv", "@a_item");
    requireResolves(f, "/top.sv", "@ub", "Item", "/pkgs.sv", "@b_item");
}

TEST_CASE("resolveSymbolAt: the qualifier itself resolves to the package",
          "[resolution][scope-op]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/top.sv",
          "module top;\n"
          "  logic pkg_b; // a same-named signal must not win before '::'\n"
          "  pkg_b::Item b; // @use\n"
          "endmodule\n");
    requireResolves(f, "/top.sv", "@use", "pkg_b", "/pkgs.sv", "@b_pkg");
}

TEST_CASE("resolveSymbolAt: pkg::function and pkg::Class::static_method",
          "[resolution][scope-op]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/top.sv",
          "module top;\n"
          "  int r;\n"
          "  initial r = pkg_b::make(); // @make\n"
          "  initial r = pkg_b::Item::stat(); // @stat\n"
          "endmodule\n");
    requireResolves(f, "/top.sv", "@make", "make", "/pkgs.sv", "@b_make");
    requireResolves(f, "/top.sv", "@stat", "stat(", "/pkgs.sv", "@b_stat");
}

TEST_CASE("resolveSymbolAt: Class::method picks the named class over an earlier same-named "
          "method", "[resolution][scope-op]")
{
    F f;
    f.add("/t.sv",
          "class First;\n"
          "  static function int build(); return 1; endfunction\n"
          "endclass\n"
          "class Second;\n"
          "  static function int build(); return 2; endfunction // @decl\n"
          "endclass\n"
          "module top;\n"
          "  int r;\n"
          "  initial r = Second::build(); // @use\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "build", "/t.sv", "@decl");
}

TEST_CASE("resolveSymbolAt: an understood qualifier with no such member resolves to nothing",
          "[resolution][scope-op]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/top.sv",
          "module top;\n"
          "  int r;\n"
          "  initial r = pkg_b::only_in_a(); // @use\n"
          "endmodule\n"
          "package pkg_c; function int only_in_a(); return 0; endfunction endpackage\n");
    auto r = resolveAt(f, "/top.sv", posOf(f.files.at("/top.sv"), "@use", "only_in_a"));
    CHECK_FALSE(r.has_value());
}

TEST_CASE("resolveSymbolAt: an unknown qualifier falls back to the name-only lookup",
          "[resolution][scope-op][fallback]")
{
    F f;
    f.add("/t.sv",
          "class Known; static function int f(); return 0; endfunction endclass // @decl\n"
          "module top;\n"
          "  int r;\n"
          "  initial r = not_a_scope::Known; // @use\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "Known", "/t.sv", "@decl", /*exact=*/false);
}

// ---------------------------------------------------------------------------
// imports
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: wildcard and specific imports pick the imported package",
          "[resolution][import]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/w.sv",
          "import pkg_b::*;\n"
          "module w;\n"
          "  Item x; // @type\n"
          "  int r;\n"
          "  initial r = make(); // @call\n"
          "endmodule\n");
    f.add("/s.sv",
          "import pkg_b::Item;\n"
          "module s;\n"
          "  Item x; // @type\n"
          "endmodule\n");
    requireResolves(f, "/w.sv", "@type", "Item", "/pkgs.sv", "@b_item");
    requireResolves(f, "/w.sv", "@call", "make", "/pkgs.sv", "@b_make");
    requireResolves(f, "/s.sv", "@type", "Item", "/pkgs.sv", "@b_item");
}

TEST_CASE("resolveSymbolAt: an import in another module of the same file doesn't apply",
          "[resolution][import]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    const std::string text =
        "module imp_a;\n"
        "  import pkg_b::make;\n"
        "endmodule\n"
        "module imp_b;\n"
        "  int r;\n"
        "  initial r = make(); // @use\n"
        "endmodule\n";
    f.add("/imp.sv", text);
    // Not imported here: only the name-only fallback finds a `make`.
    auto r = resolveAt(f, "/imp.sv", posOf(text, "@use", "make"));
    REQUIRE(r.has_value());
    CHECK_FALSE(r->exact);
}

TEST_CASE("resolveSymbolAt: a specific import outranks a wildcard import of another package",
          "[resolution][import]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/t.sv",
          "import pkg_a::*;\n"
          "import pkg_b::Item;\n"
          "module t;\n"
          "  Item x; // @use\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "Item", "/pkgs.sv", "@b_item");
}

TEST_CASE("resolveSymbolAt: a local declaration outranks a wildcard-imported one",
          "[resolution][import][lexical]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/t.sv",
          "import pkg_b::*;\n"
          "module t;\n"
          "  int make; // @decl\n"
          "  initial make = 1; // @use\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "make", "/t.sv", "@decl");
}

// ---------------------------------------------------------------------------
// . member access
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: obj.method and obj.field use obj's declared class",
          "[resolution][dot]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/top.sv",
          "module top;\n"
          "  pkg_b::Item x;\n"
          "  int r;\n"
          "  initial r = x.get(); // @get\n"
          "  initial r = x.val; // @val\n"
          "endmodule\n");
    requireResolves(f, "/top.sv", "@get", "get", "/pkgs.sv", "@b_get");
    requireResolves(f, "/top.sv", "@val", "val", "/pkgs.sv", "@b_val");
}

TEST_CASE("resolveSymbolAt: a receiver whose type came in through a wildcard import",
          "[resolution][dot][import]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    f.add("/top.sv",
          "import pkg_b::*;\n"
          "module top;\n"
          "  Item x;\n"
          "  int r;\n"
          "  initial r = x.get(); // @use\n"
          "endmodule\n");
    requireResolves(f, "/top.sv", "@use", "get", "/pkgs.sv", "@b_get");
}

TEST_CASE("resolveSymbolAt: inherited method, chain, queue element, this. and super.",
          "[resolution][dot]")
{
    F f;
    f.add("/parent.sv",
          "class Parent;\n"
          "  virtual function void step(); endfunction // @parent_step\n"
          "endclass\n");
    f.add("/t.sv",
          "class Unrelated;\n"
          "  function void run(); endfunction\n"
          "  function int get(); return 0; endfunction\n"
          "  int count;\n"
          "endclass\n"
          "class Base;\n"
          "  function void run(); endfunction // @base_run\n"
          "endclass\n"
          "class Child extends Base;\n"
          "endclass\n"
          "class Inner;\n"
          "  function int get(); return 1; endfunction // @inner_get\n"
          "endclass\n"
          "class Outer;\n"
          "  Inner inner;\n"
          "  Inner items[$];\n"
          "endclass\n"
          "class Counter;\n"
          "  int count; // @count\n"
          "  function void bump(); this.count++; endfunction // @this\n"
          "endclass\n"
          "class Kid extends Parent;\n"
          "  virtual function void step();\n"
          "    super.step(); // @super\n"
          "  endfunction\n"
          "endclass\n"
          "module top;\n"
          "  Child c;\n"
          "  Outer a;\n"
          "  int r;\n"
          "  initial c.run(); // @inherit\n"
          "  initial r = a.inner.get(); // @chain\n"
          "  initial r = a.items[0].get(); // @index\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@inherit", "run", "/t.sv", "@base_run");
    requireResolves(f, "/t.sv", "@chain", "get", "/t.sv", "@inner_get");
    requireResolves(f, "/t.sv", "@index", "get", "/t.sv", "@inner_get");
    requireResolves(f, "/t.sv", "@this", "count", "/t.sv", "@count");
    requireResolves(f, "/t.sv", "@super", "step", "/parent.sv", "@parent_step");
}

TEST_CASE("resolveSymbolAt: an understood receiver with no such member resolves to nothing",
          "[resolution][dot]")
{
    F f;
    f.add("/t.sv",
          "class Other; function void missing(); endfunction endclass\n"
          "class Real; endclass\n"
          "module top;\n"
          "  Real x;\n"
          "  initial x.missing(); // @use\n"
          "endmodule\n");
    auto r = resolveAt(f, "/t.sv", posOf(f.files.at("/t.sv"), "@use", "missing"));
    CHECK_FALSE(r.has_value());
}

TEST_CASE("resolveSymbolAt: a receiver that isn't a class (struct, hierarchical instance) "
          "falls back to the name-only lookup", "[resolution][dot][fallback]")
{
    F f;
    f.add("/t.sv",
          "module leaf; logic sig; endmodule // @decl\n"
          "module top;\n"
          "  struct packed { logic a; logic sig; } s;\n"
          "  leaf u_leaf();\n"
          "  logic r;\n"
          "  assign r = s.sig; // @struct\n"
          "  assign r = u_leaf.sig; // @hier\n"
          "endmodule\n");
    requireResolvesTo(f, "/t.sv", "@struct", "sig;", "/t.sv", "@decl", "sig", /*exact=*/false);
    requireResolvesTo(f, "/t.sv", "@hier", "sig;", "/t.sv", "@decl", "sig", /*exact=*/false);
}

// ---------------------------------------------------------------------------
// named connections (look like '.', aren't member access)
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: .port(sig) resolves to the instantiated module's port",
          "[resolution][named]")
{
    F f;
    f.add("/t.sv",
          "module other(input logic din); endmodule\n"
          "module leaf(input logic din, // @din\n"
          "            output logic q);\n"
          "endmodule\n"
          "module top;\n"
          "  logic s, o;\n"
          "  leaf u0(.din(s), // @use\n"
          "          .q(o));\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "din", "/t.sv", "@din");
}

TEST_CASE("resolveSymbolAt: #(.P(v)) and Mod #(..) inst (.port()) resolve to the module's "
          "parameter and port", "[resolution][named]")
{
    F f;
    f.add("/t.sv",
          "module other #(parameter int W = 1)(input logic [W-1:0] d); endmodule\n"
          "module leaf #(parameter int W = 8) // @param\n"
          "  (input logic [W-1:0] d); // @port\n"
          "endmodule\n"
          "module top;\n"
          "  logic [15:0] s;\n"
          "  leaf #(.W(16)) // @puse\n"
          "    u (.d(s)); // @duse\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@puse", "W", "/t.sv", "@param");
    requireResolves(f, "/t.sv", "@duse", "d", "/t.sv", "@port");
}

TEST_CASE("resolveSymbolAt: a named function argument resolves to the callee's parameter",
          "[resolution][named]")
{
    F f;
    f.add("/t.sv",
          "module top;\n"
          "  function int other(int n); return n; endfunction\n"
          "  function int f(int n); // @decl\n"
          "    return n;\n"
          "  endfunction\n"
          "  int r;\n"
          "  initial r = f(.n(3)); // @use\n"
          "endmodule\n");
    requireResolvesTo(f, "/t.sv", "@use", "n(", "/t.sv", "@decl", "n)");
}

TEST_CASE("resolveSymbolAt: the connected signal inside .port(sig) resolves lexically",
          "[resolution][named][lexical]")
{
    F f;
    f.add("/t.sv",
          "module leaf(input logic din); endmodule\n"
          "module m1; logic sig; endmodule\n"
          "module top;\n"
          "  logic sig; // @decl\n"
          "  leaf u0(.din(sig)); // @use\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "sig", "/t.sv", "@decl");
}

// ---------------------------------------------------------------------------
// lexical scope
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: innermost declaration wins (module, function local, argument)",
          "[resolution][lexical]")
{
    F f;
    f.add("/t.sv",
          "module m1;\n"
          "  logic clk;\n"
          "endmodule\n"
          "module m2;\n"
          "  logic clk; // @clk\n"
          "  assign clk = 1'b0; // @clk_use\n"
          "  logic a;\n"
          "  function int g(int a); // @arg\n"
          "    return a + 1; // @arg_use\n"
          "  endfunction\n"
          "endmodule\n"
          "class C;\n"
          "  int idx;\n"
          "  function void h();\n"
          "    int idx; // @local\n"
          "    idx = 3; // @local_use\n"
          "  endfunction\n"
          "endclass\n");
    requireResolves(f, "/t.sv", "@clk_use", "clk", "/t.sv", "@clk");
    requireResolves(f, "/t.sv", "@local_use", "idx", "/t.sv", "@local");

    // "a" also occurs inside "logic a"-free text on the @arg line only as the
    // parameter name, after "int ".
    auto r = resolveAt(f, "/t.sv", posOf(f.files.at("/t.sv"), "@arg_use", "a +"));
    REQUIRE(r.has_value());
    Pos want = posOf(f.files.at("/t.sv"), "@arg", "int a");
    CHECK(r->row.line == static_cast<int>(want.line) + 1);
    CHECK(r->row.col == static_cast<int>(want.col) + 4);
    CHECK(r->exact);
}

TEST_CASE("resolveSymbolAt: an inherited field used bare inside a derived class's method",
          "[resolution][lexical][dot]")
{
    F f;
    f.add("/t.sv",
          "class Unrelated; int level; endclass\n"
          "class Base;\n"
          "  int level; // @decl\n"
          "endclass\n"
          "class Derived extends Base;\n"
          "  function void f(); level = 2; endfunction // @use\n"
          "endclass\n");
    requireResolves(f, "/t.sv", "@use", "level", "/t.sv", "@decl");
}

TEST_CASE("resolveSymbolAt: the cursor on a declaration resolves to that declaration",
          "[resolution][lexical]")
{
    F f;
    f.add("/pkgs.sv", kPkgs);
    requireResolves(f, "/pkgs.sv", "@b_item", "Item", "/pkgs.sv", "@b_item");
    requireResolves(f, "/pkgs.sv", "@b_get", "get", "/pkgs.sv", "@b_get");
    requireResolves(f, "/pkgs.sv", "@b_val", "val", "/pkgs.sv", "@b_val");
}

TEST_CASE("resolveSymbolAt: a name nothing visible declares falls back to name-only",
          "[resolution][fallback]")
{
    F f;
    f.add("/lib.sv",
          "package lib_pkg; class LibCls; endclass endpackage // @decl\n");
    f.add("/t.sv",
          "module t;\n"
          "  LibCls x; // @use -- no import of lib_pkg\n"
          "endmodule\n");
    requireResolves(f, "/t.sv", "@use", "LibCls", "/lib.sv", "@decl", /*exact=*/false);
}

TEST_CASE("resolveSymbolAt: not on an identifier, or an unknown name, resolves to nothing",
          "[resolution]")
{
    F f;
    f.add("/t.sv", "module t;\n  assign x = undeclared_thing;\nendmodule\n");
    CHECK_FALSE(resolveSymbolAt(f.sdb, "/t.sv", f.files.at("/t.sv"), 1, 0).has_value());
    CHECK_FALSE(resolveSymbolAt(f.sdb, "/t.sv", f.files.at("/t.sv"), 1, 14).has_value());
}

// ---------------------------------------------------------------------------
// `include
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: pkg::Class and obj.method into a class `include`d in a package",
          "[resolution][include]")
{
    F f;
    const std::string inc =
        "class PkgIncClass; // @decl\n"
        "  function void hello(); endfunction // @hello\n"
        "endclass\n";
    const std::string incPath = writeTemp("pkg_inc_class.svh", inc);
    f.files[incPath] = inc;
    f.add("/inc_pkg.sv",
          "package inc_pkg;\n"
          "  `include \"" + incPath + "\"\n"
          "endpackage\n");
    f.add("/decoy.sv",
          "class PkgIncClass; function void hello(); endfunction endclass\n");
    f.add("/top.sv",
          "module top;\n"
          "  inc_pkg::PkgIncClass c; // @use\n"
          "  initial c.hello(); // @call\n"
          "endmodule\n");
    requireResolves(f, "/top.sv", "@use", "PkgIncClass", incPath, "@decl");
    requireResolves(f, "/top.sv", "@call", "hello", incPath, "@hello");
}

TEST_CASE("resolveSymbolAt: a cursor inside a comment or string literal resolves to nothing",
          "[resolution]")
{
    F f;
    f.add("/t.sv",
          "module t;\n"
          "  logic put; // put @line\n"
          "  /* put */ string s = \"put\"; // @block\n"
          "endmodule\n");
    const std::string& text = f.files.at("/t.sv");
    const Pos lineComment = posOf(text, "@line", "// put");
    CHECK_FALSE(resolveAt(f, "/t.sv", {lineComment.line, lineComment.col + 3}).has_value());
    const Pos block = posOf(text, "@block", "/* put");
    CHECK_FALSE(resolveAt(f, "/t.sv", {block.line, block.col + 3}).has_value());
    const Pos str = posOf(text, "@block", "\"put");
    CHECK_FALSE(resolveAt(f, "/t.sv", {str.line, str.col + 1}).has_value());
    // ...while the real declaration on the same line still resolves.
    const Pos decl = posOf(text, "@line", "put");
    CHECK(resolveAt(f, "/t.sv", decl).has_value());
}

// §6.30 follow-up: constructors. `new` resolves by context only -- the
// assigned variable's class, `super.`, `C::` or the declaration itself --
// never by name, which would pick whichever class's constructor came first.
const std::string kCtors =
    "package ctor_p;\n"
    "  class CtorBase;\n"
    "    function new(int base_arg); endfunction // @base_new\n"
    "  endclass\n"
    "  class CtorA extends CtorBase;\n"
    "    extern function new(string a_arg); // @a_proto\n"
    "  endclass\n"
    "  function CtorA::new(string a_arg); // @a_body\n"
    "    super.new(1); // @super\n"
    "  endfunction\n"
    "  class CtorNoCtor extends CtorBase;\n"
    "    function void f(); super.new(2); endfunction // @super_base\n"
    "  endclass\n"
    "  class CtorPlain;\n"
    "    function void f(); super.new(); endfunction // @super_none\n"
    "  endclass\n"
    "  class CtorB;\n"
    "    function new(int b_arg); endfunction // @b_new\n"
    "    CtorA fld;\n"
    "    function void run();\n"
    "      CtorA x = new(\"x\"); // @decl_init\n"
    "      x = new(\"y\"); // @assign\n"
    "      fld = new(\"z\"); // @member\n"
    "      this.fld = new(\"w\"); // @this_member\n"
    "    endfunction\n"
    "  endclass\n"
    "endpackage\n"
    "module ctor_top;\n"
    "  ctor_p::CtorB b = new(3); // @qualified_decl\n"
    "  int arr[];\n"
    "  initial begin\n"
    "    arr = new[4]; // @dyn\n"
    "  end\n"
    "endmodule\n";

TEST_CASE("resolveSymbolAt: a constructor's own declaration resolves to its first declaration",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctor.sv", kCtors);
    requireResolves(f, "/ctor.sv", "@base_new", "new", "/ctor.sv", "@base_new");
    requireResolves(f, "/ctor.sv", "@a_proto", "new", "/ctor.sv", "@a_proto");
    // The out-of-class body resolves to its extern prototype, like a method.
    requireResolves(f, "/ctor.sv", "@a_body", "new", "/ctor.sv", "@a_proto");
}

TEST_CASE("resolveSymbolAt: `lhs = new` resolves to the constructor of the assigned "
          "variable's class, not the enclosing class's",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctor.sv", kCtors);
    requireResolves(f, "/ctor.sv", "@decl_init", "new", "/ctor.sv", "@a_proto");
    requireResolves(f, "/ctor.sv", "@assign", "new", "/ctor.sv", "@a_proto");
    requireResolves(f, "/ctor.sv", "@member", "new", "/ctor.sv", "@a_proto");
    requireResolves(f, "/ctor.sv", "@this_member", "new", "/ctor.sv", "@a_proto");
    requireResolves(f, "/ctor.sv", "@qualified_decl", "new", "/ctor.sv", "@b_new");
}

TEST_CASE("resolveSymbolAt: super.new resolves to the parent's own constructor only",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctor.sv", kCtors);
    // From an out-of-class body and from an in-class method.
    requireResolves(f, "/ctor.sv", "@super", "new", "/ctor.sv", "@base_new");
    requireResolves(f, "/ctor.sv", "@super_base", "new", "/ctor.sv", "@base_new");
    // No parent class: nothing.
    const std::string& text = f.files.at("/ctor.sv");
    CHECK_FALSE(resolveAt(f, "/ctor.sv", posOf(text, "@super_none", "new")).has_value());
}

TEST_CASE("resolveSymbolAt: `new` with no class context resolves to nothing",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctor.sv", kCtors);
    const std::string& text = f.files.at("/ctor.sv");
    CHECK_FALSE(resolveAt(f, "/ctor.sv", posOf(text, "@dyn", "new")).has_value());
}

// §6.30 follow-up: `return new` takes the enclosing function's return type,
// and `new` as an argument the callee's parameter type (by position or by
// `.name(`). Two classes with constructors, so a name-only pick is wrong
// half the time.
const std::string kCtorCtx =
    "package ctx_p;\n"
    "  class CxA;\n"
    "    function new(string a); endfunction // @a_new\n"
    "  endclass\n"
    "  class CxB;\n"
    "    function new(int b); endfunction // @b_new\n"
    "  endclass\n"
    "  class CxW;\n"
    "    function new(CxB wb); endfunction // @w_new\n"
    "  endclass\n"
    "  class CxUser;\n"
    "    function void take(int n, CxA a, CxB b); endfunction\n"
    "    function CxB make_b();\n"
    "      return new(1); // @ret\n"
    "    endfunction\n"
    "    extern function CxA make_ooc();\n"
    "    function void make_none();\n"
    "      return new(2); // @ret_void\n"
    "    endfunction\n"
    "    function void use();\n"
    "      take(1, new(\"p\"), new(2)); // @pos\n"
    "      take(.b(new(3)), .a(new(\"n\")), .n(0)); // @named\n"
    "      CxW w = new(new(4)); // @ctor_arg\n"
    "      unknown_f(new(5)); // @unknown\n"
    "    endfunction\n"
    "  endclass\n"
    "  function CxA CxUser::make_ooc();\n"
    "    return new(\"o\"); // @ret_ooc\n"
    "  endfunction\n"
    "endpackage\n";

TEST_CASE("resolveSymbolAt: `return new` resolves to the constructor of the function's "
          "return type",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctx.sv", kCtorCtx);
    requireResolves(f, "/ctx.sv", "@ret", "new", "/ctx.sv", "@b_new");
    // An out-of-class body: the return type comes from its own scope chain.
    requireResolves(f, "/ctx.sv", "@ret_ooc", "new", "/ctx.sv", "@a_new");
    // A void function returns no class.
    const std::string& text = f.files.at("/ctx.sv");
    CHECK_FALSE(resolveAt(f, "/ctx.sv", posOf(text, "@ret_void", "new")).has_value());
}

TEST_CASE("resolveSymbolAt: `new` as an argument resolves to the constructor of the "
          "parameter's type",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctx.sv", kCtorCtx);
    requireResolvesTo(f, "/ctx.sv", "@pos", "new(\"p\")", "/ctx.sv", "@a_new",
                      "new");
    requireResolvesTo(f, "/ctx.sv", "@pos", "new(2)", "/ctx.sv", "@b_new",
                      "new");
    requireResolvesTo(f, "/ctx.sv", "@named", "new(3)", "/ctx.sv", "@b_new",
                      "new");
    requireResolvesTo(f, "/ctx.sv", "@named", "new(\"n\")", "/ctx.sv", "@a_new",
                      "new");
    // An argument to a constructor: the outer `new` is resolved first.
    requireResolvesTo(f, "/ctx.sv", "@ctor_arg", "new(4)", "/ctx.sv", "@b_new",
                      "new");
    requireResolvesTo(f, "/ctx.sv", "@ctor_arg", "new(new", "/ctx.sv", "@w_new",
                      "new");
    // A callee nothing declares: nothing.
    const std::string& text = f.files.at("/ctx.sv");
    CHECK_FALSE(resolveAt(f, "/ctx.sv", posOf(text, "@unknown", "new")).has_value());
}

TEST_CASE("overrideFamilyId: a constructor is its own family, shared by prototype and body",
          "[resolution][ctor]")
{
    F f;
    f.add("/ctor.sv", kCtors);
    const std::string& text = f.files.at("/ctor.sv");
    auto proto = resolveAt(f, "/ctor.sv", posOf(text, "@a_proto", "new"));
    auto base  = resolveAt(f, "/ctor.sv", posOf(text, "@base_new", "new"));
    REQUIRE(proto.has_value());
    REQUIRE(base.has_value());
    CHECK(overrideFamilyId(f.sdb, proto->row) == proto->row.id);
    CHECK(overrideFamilyId(f.sdb, base->row) == base->row.id);
    // The out-of-class body row joins its prototype's family.
    for (auto& r : f.sdb.findSymbolsByName("new"))
        if (r.scope == "ctor_p::CtorA") CHECK(overrideFamilyId(f.sdb, r) == proto->row.id);
}

// §6.30 follow-up: a Typedef that aliases a class is followed to the class,
// as a receiver's type and as a `::` qualifier (`uvm_config_int::set` is a
// typedef of a parameterized uvm_config_db). Every case has a same-named
// decoy method a name-only lookup could pick.
const std::string kTypedefs =
    "package td_p;\n"
    "  class TdC;\n"
    "    function int td_get(); return 0; endfunction // @get\n"
    "    static function void td_s(); endfunction // @s\n"
    "  endclass\n"
    "  class TdP #(type T = int);\n"
    "    static function void td_ps(); endfunction // @ps\n"
    "  endclass\n"
    "  typedef TdC td_alias_t;\n"
    "  typedef td_alias_t td_alias2_t;\n"
    "  typedef TdP#(int) tdp_int_t;\n"
    "endpackage\n"
    "class TdDecoy;\n"
    "  function int td_get(); return 1; endfunction\n"
    "  static function void td_s(); endfunction\n"
    "  static function void td_ps(); endfunction\n"
    "endclass\n"
    "module td_top;\n"
    "  import td_p::*;\n"
    "  td_alias_t a;\n"
    "  td_alias2_t b;\n"
    "  initial begin\n"
    "    void'(a.td_get()); // @recv\n"
    "    void'(b.td_get()); // @recv2\n"
    "    td_alias_t::td_s(); // @qual\n"
    "    tdp_int_t::td_ps(); // @param\n"
    "  end\n"
    "endmodule\n";

TEST_CASE("resolveSymbolAt: a receiver whose type is a class typedef resolves into the class",
          "[resolution][typedef]")
{
    F f;
    f.add("/td.sv", kTypedefs);
    requireResolves(f, "/td.sv", "@recv", "td_get", "/td.sv", "@get");
    requireResolves(f, "/td.sv", "@recv2", "td_get", "/td.sv", "@get");
}

TEST_CASE("resolveSymbolAt: a class typedef as a `::` qualifier, parameterized or not",
          "[resolution][typedef]")
{
    F f;
    f.add("/td.sv", kTypedefs);
    requireResolves(f, "/td.sv", "@qual", "td_s", "/td.sv", "@s");
    requireResolves(f, "/td.sv", "@param", "td_ps", "/td.sv", "@ps");
}

// ---------------------------------------------------------------------------
// Block scopes (plan.md §6.30, "block scopes")
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: same-named locals in sibling blocks resolve to their own block",
          "[resolution][block]")
{
    F f;
    const std::string text =
        "module blk_m;\n"
        "  initial begin\n"
        "    int x; // @x1\n"
        "    x = 1; // @use1\n"
        "  end\n"
        "  initial begin\n"
        "    int x; // @x2\n"
        "    x = 2; // @use2\n"
        "  end\n"
        "endmodule\n";
    f.add("/blk.sv", text);
    requireResolves(f, "/blk.sv", "@use1", "x", "/blk.sv", "@x1");
    requireResolves(f, "/blk.sv", "@use2", "x", "/blk.sv", "@x2");
}

TEST_CASE("resolveSymbolAt: a block local shadows the enclosing function's",
          "[resolution][block]")
{
    F f;
    const std::string text =
        "module blk_s;\n"
        "  function int f();\n"
        "    int v; // @outer\n"
        "    begin : inner\n"
        "      int v; // @inner\n"
        "      v = 1; // @use_in\n"
        "    end\n"
        "    return v; // @use_out\n"
        "  endfunction\n"
        "endmodule\n";
    f.add("/blks.sv", text);
    requireResolves(f, "/blks.sv", "@use_in", "v", "/blks.sv", "@inner");
    requireResolves(f, "/blks.sv", "@use_out", "v", "/blks.sv", "@outer");
}

TEST_CASE("resolveSymbolAt: for and foreach loop variables resolve to their own loop",
          "[resolution][block]")
{
    F f;
    const std::string text =
        "class BlkC;\n"
        "  int q[$];\n"
        "  function void f();\n"
        "    for (int i = 0; i < 2; i++) // @for1\n"
        "      q.push_back(i); // @use_for1\n"
        "    for (int i = 0; i < 3; i++) begin // @for2\n"
        "      q.push_back(i); // @use_for2\n"
        "    end\n"
        "    foreach (q[i]) // @fe\n"
        "      q[i] = 0; // @use_fe\n"
        "  endfunction\n"
        "endclass\n";
    f.add("/blkl.sv", text);
    requireResolvesTo(f, "/blkl.sv", "@use_for1", "i)", "/blkl.sv", "@for1", "i =");
    requireResolvesTo(f, "/blkl.sv", "@use_for2", "i)", "/blkl.sv", "@for2", "i =");
    requireResolvesTo(f, "/blkl.sv", "@use_fe", "i]", "/blkl.sv", "@fe", "i]");
}

TEST_CASE("resolveSymbolAt: a block local is not visible after its block",
          "[resolution][block]")
{
    F f;
    const std::string text =
        "module blk_v;\n"
        "  int r;\n"
        "  initial begin\n"
        "    begin\n"
        "      int gone;\n"
        "    end\n"
        "    r = gone; // @use\n"
        "  end\n"
        "endmodule\n";
    f.add("/blkv.sv", text);
    // Only the name-only fallback finds it.
    auto r = resolveAt(f, "/blkv.sv", posOf(text, "@use", "gone"));
    REQUIRE(r.has_value());
    CHECK_FALSE(r->exact);
}

TEST_CASE("resolveSymbolAt: return new(...) in a block resolves the function's return type",
          "[resolution][block]")
{
    F f;
    const std::string text =
        "class BlkR; // @cls\n"
        "  function new(int a); endfunction // @ctor\n"
        "  static function BlkR make();\n"
        "    begin\n"
        "      int tmp;\n"
        "      return new(tmp); // @use\n"
        "    end\n"
        "  endfunction\n"
        "endclass\n";
    f.add("/blkr.sv", text);
    requireResolves(f, "/blkr.sv", "@use", "new", "/blkr.sv", "@ctor");
}

// ---------------------------------------------------------------------------
// Column-granular scopes (plan.md §6.30, "column-granular scopes")
// ---------------------------------------------------------------------------

TEST_CASE("resolveSymbolAt: an outer local used after a same-line inner block",
          "[resolution][scope-col]")
{
    F f;
    const std::string text =
        "module col_m;\n"
        "  initial begin int v; begin int v; v = 1; end v = 2; end // @line\n"
        "endmodule\n";
    f.add("/col.sv", text);
    // `v = 1` is the inner block's, `v = 2` (after the inner `end`) the outer's.
    requireResolvesTo(f, "/col.sv", "@line", "v = 1", "/col.sv", "@line", "v; v = 1");
    requireResolvesTo(f, "/col.sv", "@line", "v = 2", "/col.sv", "@line", "v; begin");
}

TEST_CASE("resolveSymbolAt: an argument isn't visible after its function's endfunction "
          "on the same line",
          "[resolution][scope-col]")
{
    F f;
    const std::string text =
        "module col_a;\n"
        "  function int f(int arg); return arg; endfunction int r = arg; // @line\n"
        "endmodule\n";
    f.add("/cola.sv", text);
    requireResolvesTo(f, "/cola.sv", "@line", "arg;", "/cola.sv", "@line", "arg)");
    // After `endfunction` only the name-only fallback finds it.
    const std::string& t = f.files.at("/cola.sv");
    const Pos line = posOf(t, "@line", "r = arg");
    auto r = resolveAt(f, "/cola.sv", {line.line, line.col + 4});
    REQUIRE(r.has_value());
    CHECK_FALSE(r->exact);
}
