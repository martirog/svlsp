// Scope-strict go-to-definition tests, driven through a real
// CompilationController::compile (not hand-built rows).
//
// DefinitionProvider resolves the bare word under the cursor via
// findSymbolsByName + pickBestSymbol (same file first, then a
// declaration-like kind, then path/line order). The older tests in
// test_definition.cpp only ever had one candidate per name, so they could
// not tell a scoped resolution from a lucky first-row pick. Every case here
// deliberately declares a same-named *decoy* that a name-only lookup would
// find first, and asserts the exact target line/column -- never just
// "non-null".
//
// Cases the current name-only resolution gets wrong are tagged
// [!shouldfail] (Catch2 reports them as passing while they fail, and as a
// failure the moment they start passing) so the suite stays green while the
// gap stays visible. Remove the tag when definition becomes scope-aware.

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
          "[definition][scoped][scope-op][!shouldfail]")
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
          "[definition][scoped][scope-op][!shouldfail]")
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
          "[definition][scoped][scope-op][!shouldfail]")
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

TEST_CASE("Definition: Class::method() picks the named class's method over an earlier "
          "same-named method on another class in the same file",
          "[definition][scoped][scope-op][!shouldfail]")
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
          "[definition][scoped][import][!shouldfail]")
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
          "[definition][scoped][import][!shouldfail]")
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
          "[definition][scoped][import][!shouldfail]")
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
          "[definition][scoped][include][scope-op][!shouldfail]")
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
          "[definition][scoped][dot][!shouldfail]")
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
          "[definition][scoped][dot][!shouldfail]")
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
          "[definition][scoped][dot][!shouldfail]")
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
          "[definition][scoped][dot][!shouldfail]")
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
          "[definition][scoped][dot][!shouldfail]")
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
          "[definition][scoped][dot][!shouldfail]")
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
          "[definition][scoped][lexical][!shouldfail]")
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
          "[definition][scoped][lexical][!shouldfail]")
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
          "[definition][scoped][lexical][!shouldfail]")
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
