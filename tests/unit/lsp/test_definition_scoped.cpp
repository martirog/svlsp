// Scope-strict go-to-definition tests, driven through a real
// CompilationController::compile (not hand-built rows).
//
// DefinitionProvider resolves the identifier under the cursor with
// resolveSymbolAt (plan.md §6.30): `::` qualifiers, imports, `.` receiver
// chains and the lexical scope. Before that it used findSymbolsByName +
// pickBestSymbol (same file first, then a declaration-like kind, then
// path/line order). The older tests in test_definition.cpp only ever had
// one candidate per name, so they could not tell a scoped resolution from
// a lucky first-row pick. Every case here deliberately declares a
// same-named *decoy* that a name-only lookup would find first, and asserts
// the exact target line/column -- never just "non-null".

#include <catch2/catch_test_macros.hpp>
#include "lsp/definition.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct RealCompileFixture {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController ctrl{sdb};
    RealCompileFixture() { db.initSchema(); }
};

struct Pos { unsigned line; unsigned col; };

// Position of `token` on the first line of `text` containing `marker`.
// Markers are trailing `// @name` comments so tests never hardcode
// line/column numbers that silently drift when a fixture is edited.
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

lsp::DefinitionParams makeParams(const std::string& path, Pos p)
{
    lsp::DefinitionParams params;
    params.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    params.position.line      = p.line;
    params.position.character = p.col;
    return params;
}

// Go-to-definition from `from` in `fromText`, asserting it lands exactly on
// `want` in `wantPath`.
void requireDefinitionAt(SymbolDatabase& sdb,
                         const std::string& fromPath, const std::string& fromText, Pos from,
                         const std::string& wantPath, Pos want)
{
    auto result = DefinitionProvider::getDefinition(makeParams(fromPath, from), sdb, fromText);
    REQUIRE_FALSE(result.isNull());
    const auto& loc = std::get<lsp::Location>(result.get<lsp::Definition>());
    CHECK(std::string(loc.uri.path()) == wantPath);
    CHECK(loc.range.start.line == want.line);
    CHECK(loc.range.start.character == want.col);
}

// Writes `content` to a fresh file under the system temp dir and returns
// its absolute path. Names are prefixed so a leftover file is recognizable.
std::string writeTemp(const std::string& name, const std::string& content)
{
    const auto dir = std::filesystem::temp_directory_path() / "svlsp_test_def_scoped";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::ofstream(path) << content;
    return path;
}

// Two packages each declaring a class `Item` with a method `get` and a
// function `make`, compiled from their own file so pickBestSymbol's
// same-file preference can't accidentally pick the right one.
const std::string kPkgs =
    "package pkg_a;\n"
    "  class Item; // @a_item\n"
    "    int val; // @a_val\n"
    "    function int get(); return 1; endfunction // @a_get\n"
    "    static function int stat(); return 1; endfunction // @a_stat\n"
    "  endclass\n"
    "  function int make(); return 1; endfunction // @a_make\n"
    "endpackage\n"
    "\n"
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
// `::`-qualified references
// ---------------------------------------------------------------------------

TEST_CASE("Definition: cursor on the package half of pkg::Item lands on the package",
          "[definition][scoped][scope-op]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  pkg_b::Item x; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "pkg_b"),
                        "/pkgs.sv", posOf(kPkgs, "@b_pkg", "pkg_b"));
}

TEST_CASE("Definition: pkg_a::Item resolves to pkg_a's class, not pkg_b's",
          "[definition][scoped][scope-op]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  pkg_a::Item x; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "Item"),
                        "/pkgs.sv", posOf(kPkgs, "@a_item", "Item"));
}

TEST_CASE("Definition: pkg_b::Item resolves to pkg_b's class, not the first-declared pkg_a one",
          "[definition][scoped][scope-op]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  pkg_b::Item x; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "Item"),
                        "/pkgs.sv", posOf(kPkgs, "@b_item", "Item"));
}

TEST_CASE("Definition: pkg_b::make() resolves to pkg_b's package function",
          "[definition][scoped][scope-op]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  int r;\n"
        "  initial r = pkg_b::make(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "make"),
                        "/pkgs.sv", posOf(kPkgs, "@b_make", "make"));
}

TEST_CASE("Definition: pkg_b::Item::stat() resolves to pkg_b's static method",
          "[definition][scoped][scope-op]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  int r;\n"
        "  initial r = pkg_b::Item::stat(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "stat("),
                        "/pkgs.sv", posOf(kPkgs, "@b_stat", "stat("));
}

TEST_CASE("Definition: pkg::Class::type_id::create() resolves each segment through the "
          "class's own typedef, not an earlier same-named one",
          "[definition][scoped][scope-op]")
{
    // The UVM factory shape: `type_id` is a class-scoped typedef of a
    // registry class. The decoys in tq_a are declared first, so a name-only
    // lookup would land on them.
    RealCompileFixture f;
    const std::string pkgs =
        "package tq_a;\n"
        "  class Reg;\n"
        "    static function int create(); return 1; endfunction\n"
        "  endclass\n"
        "  class User;\n"
        "    typedef Reg type_id;\n"
        "  endclass\n"
        "endpackage\n"
        "package tq_b;\n"
        "  class Reg;\n"
        "    static function int create(); return 2; endfunction // @b_create\n"
        "  endclass\n"
        "  class User;\n"
        "    typedef Reg type_id; // @b_typeid\n"
        "  endclass\n"
        "endpackage\n";
    f.ctrl.compile("/pkgs.sv", pkgs);
    const std::string top =
        "module top;\n"
        "  int r;\n"
        "  initial r = tq_b::User::type_id::create(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "type_id"),
                        "/pkgs.sv", posOf(pkgs, "@b_typeid", "type_id"));
    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "create"),
                        "/pkgs.sv", posOf(pkgs, "@b_create", "create"));
}

TEST_CASE("Definition: Class::method() picks the named class's method over an earlier "
          "same-named method on another class in the same file",
          "[definition][scoped][scope-op]")
{
    RealCompileFixture f;
    const std::string src =
        "class First;\n"
        "  static function int build(); return 1; endfunction // @first\n"
        "endclass\n"
        "class Second;\n"
        "  static function int build(); return 2; endfunction // @second\n"
        "endclass\n"
        "module top;\n"
        "  int r;\n"
        "  initial r = Second::build(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "build"),
                        "/t.sv", posOf(src, "@second", "build"));
}

// ---------------------------------------------------------------------------
// Imports: a bare name must resolve to what the import actually brings in
// ---------------------------------------------------------------------------

TEST_CASE("Definition: bare Item after `import pkg_b::*;` resolves to pkg_b's class",
          "[definition][scoped][import]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "import pkg_b::*;\n"
        "module top;\n"
        "  Item x; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "Item"),
                        "/pkgs.sv", posOf(kPkgs, "@b_item", "Item"));
}

TEST_CASE("Definition: bare Item after `import pkg_b::Item;` resolves to pkg_b's class",
          "[definition][scoped][import]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "import pkg_b::Item;\n"
        "module top;\n"
        "  Item x; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "Item"),
                        "/pkgs.sv", posOf(kPkgs, "@b_item", "Item"));
}

TEST_CASE("Definition: a bare call after `import pkg_b::*;` resolves to pkg_b's function",
          "[definition][scoped][import]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "import pkg_b::*;\n"
        "module top;\n"
        "  int r;\n"
        "  initial r = make(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "make"),
                        "/pkgs.sv", posOf(kPkgs, "@b_make", "make"));
}

// ---------------------------------------------------------------------------
// `include
// ---------------------------------------------------------------------------

TEST_CASE("Definition: a class declared in an `include`d file lands in that file at its "
          "original line, not the expanded-buffer line",
          "[definition][scoped][include]")
{
    RealCompileFixture f;
    const std::string inc =
        "// leading comment lines shift the class down in the included file\n"
        "//\n"
        "//\n"
        "class IncClass; // @decl\n"
        "  int v;\n"
        "endclass\n";
    const std::string incPath = writeTemp("inc_class.svh", inc);

    const std::string top =
        "// the include sits a few lines down in the primary file too\n"
        "\n"
        "`include \"" + incPath + "\"\n"
        "module top;\n"
        "  IncClass c; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "IncClass"),
                        incPath, posOf(inc, "@decl", "IncClass"));
}

TEST_CASE("Definition: a symbol declared in the primary file *after* an `include keeps "
          "its own primary-file line",
          "[definition][scoped][include]")
{
    RealCompileFixture f;
    const std::string inc =
        "class A1; endclass\n"
        "class A2; endclass\n"
        "class A3; endclass\n"
        "class A4; endclass\n";
    const std::string incPath = writeTemp("four_classes.svh", inc);

    const std::string top =
        "`include \"" + incPath + "\"\n"
        "module top;\n"
        "  logic after_inc; // @decl\n"
        "  assign after_inc = 1'b0; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "after_inc"),
                        "/top.sv", posOf(top, "@decl", "after_inc"));
}

TEST_CASE("Definition: a class `include`d inside a package (UVM layout) lands in the "
          "included file, and pkg::Class from another file resolves to it",
          "[definition][scoped][include][scope-op]")
{
    RealCompileFixture f;
    const std::string inc =
        "class PkgIncClass; // @decl\n"
        "  function void hello(); endfunction // @hello\n"
        "endclass\n";
    const std::string incPath = writeTemp("pkg_inc_class.svh", inc);

    const std::string pkg =
        "package inc_pkg;\n"
        "  `include \"" + incPath + "\"\n"
        "endpackage\n";
    f.ctrl.compile("/inc_pkg.sv", pkg);

    const std::string top =
        "module top;\n"
        "  inc_pkg::PkgIncClass c; // @use\n"
        "  initial c.hello(); // @call\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "PkgIncClass"),
                        incPath, posOf(inc, "@decl", "PkgIncClass"));
    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@call", "hello"),
                        incPath, posOf(inc, "@hello", "hello"));
}

TEST_CASE("Definition: a same-named class in the primary file does not hijack a use that "
          "the `include`d package's class should resolve to",
          "[definition][scoped][include][scope-op]")
{
    RealCompileFixture f;
    const std::string inc =
        "class Shadowed; // @decl\n"
        "endclass\n";
    const std::string incPath = writeTemp("shadowed.svh", inc);

    const std::string top =
        "package p;\n"
        "  `include \"" + incPath + "\"\n"
        "endpackage\n"
        "class Shadowed;\n"   // decoy in the primary file
        "endclass\n"
        "module top;\n"
        "  p::Shadowed s; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "Shadowed"),
                        incPath, posOf(inc, "@decl", "Shadowed"));
}

// ---------------------------------------------------------------------------
// `.` member access
// ---------------------------------------------------------------------------

TEST_CASE("Definition: obj.get() on the first-declared class resolves to its own method",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  pkg_a::Item x;\n"
        "  int r;\n"
        "  initial r = x.get(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "get"),
                        "/pkgs.sv", posOf(kPkgs, "@a_get", "get"));
}

TEST_CASE("Definition: obj.get() resolves to the method on obj's declared class, not an "
          "earlier same-named method on an unrelated class",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  pkg_b::Item x;\n"
        "  int r;\n"
        "  initial r = x.get(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "get"),
                        "/pkgs.sv", posOf(kPkgs, "@b_get", "get"));
}

TEST_CASE("Definition: obj.val resolves to the field on obj's declared class",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    f.ctrl.compile("/pkgs.sv", kPkgs);
    const std::string top =
        "module top;\n"
        "  pkg_b::Item x;\n"
        "  int r;\n"
        "  initial r = x.val; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/top.sv", top);

    requireDefinitionAt(f.sdb, "/top.sv", top, posOf(top, "@use", "val"),
                        "/pkgs.sv", posOf(kPkgs, "@b_val", "val"));
}

TEST_CASE("Definition: a method reached through inheritance resolves to the ancestor's "
          "declaration, not an unrelated same-named method declared earlier",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    const std::string src =
        "class Unrelated;\n"
        "  function void run(); endfunction\n"   // decoy, declared first
        "endclass\n"
        "class Base;\n"
        "  function void run(); endfunction // @base_run\n"
        "endclass\n"
        "class Child extends Base;\n"
        "endclass\n"
        "module top;\n"
        "  Child c;\n"
        "  initial c.run(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "run"),
                        "/t.sv", posOf(src, "@base_run", "run"));
}

TEST_CASE("Definition: a two-segment chain a.inner.get() resolves through the field's type",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    const std::string src =
        "class Decoy;\n"
        "  function int get(); return 0; endfunction\n"   // decoy, declared first
        "endclass\n"
        "class Inner;\n"
        "  function int get(); return 1; endfunction // @inner_get\n"
        "endclass\n"
        "class Outer;\n"
        "  Inner inner; // @inner_field\n"
        "endclass\n"
        "module top;\n"
        "  Outer a;\n"
        "  int r;\n"
        "  initial r = a.inner.get(); // @use\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "get"),
                        "/t.sv", posOf(src, "@inner_get", "get"));
}

TEST_CASE("Definition: this.field inside a method resolves to the enclosing class's field",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    const std::string src =
        "class Other;\n"
        "  int count;\n"                                  // decoy, declared first
        "endclass\n"
        "class Counter;\n"
        "  int count; // @decl\n"
        "  function void bump(); this.count++; endfunction // @use\n"
        "endclass\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "count"),
                        "/t.sv", posOf(src, "@decl", "count"));
}

TEST_CASE("Definition: super.method() resolves to the parent's method, not the override",
          "[definition][scoped][dot]")
{
    RealCompileFixture f;
    // Parent lives in its own file so the override is the same-file
    // candidate a name-only lookup prefers.
    const std::string parent =
        "class Parent;\n"
        "  virtual function void step(); endfunction // @parent_step\n"
        "endclass\n";
    f.ctrl.compile("/parent.sv", parent);
    const std::string src =
        "class Kid extends Parent;\n"
        "  virtual function void step();\n"             // the override
        "    super.step(); // @use\n"
        "  endfunction\n"
        "endclass\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "step"),
                        "/parent.sv", posOf(parent, "@parent_step", "step"));
}

// ---------------------------------------------------------------------------
// Lexical scoping of plain identifiers (no `::`/`.` at all)
// ---------------------------------------------------------------------------

TEST_CASE("Definition: a signal resolves to the declaration in its own module, not a "
          "same-named signal in an earlier module of the same file",
          "[definition][scoped][lexical]")
{
    RealCompileFixture f;
    const std::string src =
        "module m1;\n"
        "  logic clk;\n"             // decoy, declared first
        "endmodule\n"
        "module m2;\n"
        "  logic clk; // @decl\n"
        "  assign clk = 1'b0; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "clk"),
                        "/t.sv", posOf(src, "@decl", "clk"));
}

TEST_CASE("Definition: a function's local variable shadows a same-named class field",
          "[definition][scoped][lexical]")
{
    RealCompileFixture f;
    const std::string src =
        "class C;\n"
        "  int idx;\n"                     // class field, declared first
        "  function void f();\n"
        "    int idx; // @decl\n"
        "    idx = 3; // @use\n"
        "  endfunction\n"
        "endclass\n";
    f.ctrl.compile("/t.sv", src);

    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "idx"),
                        "/t.sv", posOf(src, "@decl", "idx"));
}

TEST_CASE("Definition: a function argument shadows a same-named module-level signal",
          "[definition][scoped][lexical]")
{
    RealCompileFixture f;
    const std::string src =
        "module top;\n"
        "  logic a;\n"                             // module signal, declared first
        "  function int f(int a); // @decl\n"
        "    return a + 1; // @use\n"
        "  endfunction\n"
        "endmodule\n";
    f.ctrl.compile("/t.sv", src);

    Pos decl = posOf(src, "@decl", "int a");
    decl.col += 4;  // skip "int " to land on the parameter name
    requireDefinitionAt(f.sdb, "/t.sv", src, posOf(src, "@use", "a +"), "/t.sv", decl);
}

// ---------------------------------------------------------------------------
// Kinds recorded since plan.md §6.30 step C: typedef, enum literal, struct
// member, genvar. Each has a same-named decoy in another module.
// ---------------------------------------------------------------------------

TEST_CASE("Definition: typedef, enum literal and genvar uses land on their own module's "
          "declarations",
          "[definition][scoped][stepc]")
{
    RealCompileFixture f;
    const std::string src =
        "module dk_m1;\n"
        "  typedef logic [3:0] dk_t;\n"          // decoys, declared first
        "  enum {DK_A, DK_B} e1;\n"
        "  genvar dk_g;\n"
        "endmodule\n"
        "module dk_m2;\n"
        "  typedef logic [7:0] dk_t; // @tdecl\n"
        "  enum {DK_A, DK_B} e2; // @edecl\n"
        "  genvar dk_g; // @gdecl\n"
        "  logic [3:0] v;\n"
        "  dk_t x; // @tuse\n"
        "  initial e2 = DK_B; // @euse\n"
        "  for (dk_g = 0; dk_g < 4; dk_g++) begin : blk // @guse\n"
        "    assign v[dk_g] = 1'b0;\n"
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/dk.sv", src);

    requireDefinitionAt(f.sdb, "/dk.sv", src, posOf(src, "@tuse", "dk_t"),
                        "/dk.sv", posOf(src, "@tdecl", "dk_t"));
    requireDefinitionAt(f.sdb, "/dk.sv", src, posOf(src, "@euse", "DK_B"),
                        "/dk.sv", posOf(src, "@edecl", "DK_B"));
    requireDefinitionAt(f.sdb, "/dk.sv", src, posOf(src, "@guse", "dk_g"),
                        "/dk.sv", posOf(src, "@gdecl", "dk_g"));
}

TEST_CASE("Definition: a struct member access lands on the member declaration",
          "[definition][scoped][stepc]")
{
    // Dot-resolution into struct types is out of scope (plan.md §6.30 C):
    // this lands through the name-only fallback, which only works because
    // the member is recorded at all now.
    RealCompileFixture f;
    const std::string src =
        "module dk_s;\n"
        "  struct packed { logic [3:0] dk_hi; logic [3:0] dk_lo; } s; // @decl\n"
        "  initial s.dk_hi = 4'h1; // @use\n"
        "endmodule\n";
    f.ctrl.compile("/dks.sv", src);

    requireDefinitionAt(f.sdb, "/dks.sv", src, posOf(src, "@use", "dk_hi"),
                        "/dks.sv", posOf(src, "@decl", "dk_hi"));
}

// ---------------------------------------------------------------------------
// Out-of-class method bodies (plan.md §6.30 step D): inside `function void
// C::m(); ... endfunction` the class's own and inherited members are
// visible, `this.`/`super.` work, and an argument resolves to the body's
// own declaration rather than the extern prototype's.
// ---------------------------------------------------------------------------

TEST_CASE("Definition: inside an out-of-class body, bare names resolve to the class's own and "
          "inherited members, this./super. work, and arguments are the body's own",
          "[definition][scoped][outofclass]")
{
    RealCompileFixture f;
    const std::string src =
        "package ooc_p;\n"
        "  int ooc_cnt;\n"                                      // decoy: package-level
        "  function void ooc_helper(); endfunction\n"           // decoy: package-level
        "  class OocBase;\n"
        "    int ooc_inh; // @inhdecl\n"
        "    virtual function void ooc_hook(); endfunction // @basehook\n"
        "  endclass\n"
        "  class OocC extends OocBase;\n"
        "    int ooc_cnt; // @flddecl\n"
        "    extern function void ooc_helper(); // @helperdecl\n"
        "    extern virtual function void ooc_hook();\n"
        "    extern function void ooc_m(int ooc_arg);\n"
        "  endclass\n"
        "  function void OocC::ooc_helper(); endfunction\n"
        "  function void OocC::ooc_hook(); endfunction\n"
        "  function void OocC::ooc_m(int ooc_arg); // @argdecl\n"
        "    ooc_cnt = ooc_arg; // @use1\n"
        "    ooc_helper(); // @use2\n"
        "    this.ooc_cnt = ooc_inh; // @use3\n"
        "    super.ooc_hook(); // @use4\n"
        "  endfunction\n"
        "endpackage\n";
    f.ctrl.compile("/ooc.sv", src);

    auto use1 = posOf(src, "@use1", "ooc_cnt");
    requireDefinitionAt(f.sdb, "/ooc.sv", src, use1, "/ooc.sv", posOf(src, "@flddecl", "ooc_cnt"));
    requireDefinitionAt(f.sdb, "/ooc.sv", src, posOf(src, "@use1", "ooc_arg"),
                        "/ooc.sv", posOf(src, "@argdecl", "ooc_arg"));
    requireDefinitionAt(f.sdb, "/ooc.sv", src, posOf(src, "@use2", "ooc_helper"),
                        "/ooc.sv", posOf(src, "@helperdecl", "ooc_helper"));
    requireDefinitionAt(f.sdb, "/ooc.sv", src, posOf(src, "@use3", "ooc_cnt"),
                        "/ooc.sv", posOf(src, "@flddecl", "ooc_cnt"));
    requireDefinitionAt(f.sdb, "/ooc.sv", src, posOf(src, "@use3", "ooc_inh"),
                        "/ooc.sv", posOf(src, "@inhdecl", "ooc_inh"));
    requireDefinitionAt(f.sdb, "/ooc.sv", src, posOf(src, "@use4", "ooc_hook"),
                        "/ooc.sv", posOf(src, "@basehook", "ooc_hook"));
}

// ---------------------------------------------------------------------------
// Macros (plan.md §6.29 follow-up)
// ---------------------------------------------------------------------------

TEST_CASE("Definition: a macro use lands on its `define, in an included header",
          "[definition][scoped][macro][phase6.29]")
{
    RealCompileFixture f;
    const auto dir = std::filesystem::temp_directory_path() / "svlsp_test_def_macro";
    std::filesystem::create_directories(dir);
    const std::string inc = (dir / "defm.svh").string();
    std::ofstream(inc) << "// header\n  `define DEFM_LOG(ID) $display(ID)\n";

    const std::string top = (dir / "defm_top.sv").string();
    const std::string text =
        "`include \"defm.svh\"\n"
        "`define DEFM_W 8\n"
        "module defm_m;\n"
        "  parameter DEFM_W = 2;\n"
        "  logic [`DEFM_W-1:0] d; // @w\n"
        "  initial `DEFM_LOG(\"x\"); // @log\n"
        "`ifdef DEFM_W // @ifdef\n"
        "`endif\n"
        "endmodule\n";
    f.ctrl.compile(top, text);

    requireDefinitionAt(f.sdb, top, text, posOf(text, "@log", "DEFM_LOG"), inc, {1, 10});
    requireDefinitionAt(f.sdb, top, text, posOf(text, "@w", "DEFM_W"), top, {1, 8});
    requireDefinitionAt(f.sdb, top, text, posOf(text, "@ifdef", "DEFM_W"), top, {1, 8});
    // The same-named parameter, used bare, is still the parameter.
    const std::string use = "  assign d = DEFM_W; // @p\n";
    const std::string text2 = text.substr(0, text.find("`ifdef")) + use +
                              text.substr(text.find("`ifdef"));
    f.ctrl.compile(top, text2);
    requireDefinitionAt(f.sdb, top, text2, posOf(text2, "@p", "DEFM_W"), top, {3, 12});
}

TEST_CASE("Definition: a redefined macro lands on the definition in effect",
          "[definition][scoped][macro][phase6.29]")
{
    RealCompileFixture f;
    const std::string text =
        "`define DEFR_V 1\n"
        "module defr_a; int x = `DEFR_V; endmodule // @first\n"
        "`undef DEFR_V\n"
        "`define DEFR_V 2\n"
        "module defr_b; int x = `DEFR_V; endmodule // @second\n";
    f.ctrl.compile("/defr.sv", text);
    requireDefinitionAt(f.sdb, "/defr.sv", text, posOf(text, "@first", "DEFR_V"),
                        "/defr.sv", {0, 8});
    requireDefinitionAt(f.sdb, "/defr.sv", text, posOf(text, "@second", "DEFR_V"),
                        "/defr.sv", {3, 8});
}

TEST_CASE("Definition: an unknown macro is null", "[definition][scoped][macro][phase6.29]")
{
    RealCompileFixture f;
    const std::string text = "module defu_m; endmodule\nint defu_m2 = `defu_m;\n";
    f.ctrl.compile("/defu.sv", text);
    auto result = DefinitionProvider::getDefinition(makeParams("/defu.sv", {1, 15}), f.sdb, text);
    CHECK(result.isNull());
}
