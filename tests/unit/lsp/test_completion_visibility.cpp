// Negative bare-name completion tests: what must NOT be offered.
//
// Positive cases elsewhere prove a name shows up; these pin that a name
// only shows up where the LRM makes it visible:
//   - a package keeps its items to itself unless the cursor is inside the
//     package or the package is imported (wildcard or the specific item);
//     a plain import is not transitive, only `export` re-exports (26.3-26.6);
//   - an import is scoped to the design unit or package it is written in;
//   - a class's methods/fields are never bare names outside the class and
//     its subclasses; a module's own functions, tasks and classes stay in
//     that module; a subroutine's arguments and locals stay in it;
//   - where a class (or a function/task) may be declared at all: `class`
//     is legal at the top level and in packages, modules, interfaces,
//     programs and (nested, 8.23) classes, but not in a function or task
//     body; a function/task can't be declared inside another one (13.3);
//   - a nested class is a bare name inside its outer class only.
//
// Every case uses an empty prefix (the cursor on a blank line), so the
// full candidate list is checked, not a fuzzy-filtered slice.

#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include <algorithm>
#include <string>
#include <vector>

namespace {

struct Fixture {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController ctrl{sdb};
    Fixture() { db.initSchema(); }
};

lsp::CompletionParams makeParams(std::string_view path, unsigned line, unsigned col)
{
    lsp::CompletionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// 0-based line of the first line containing `marker`.
unsigned lineOf(const std::string& text, const std::string& marker)
{
    const size_t at = text.find(marker);
    REQUIRE(at != std::string::npos);
    return static_cast<unsigned>(std::count(text.begin(), text.begin() + at, '\n'));
}

// Labels offered with an empty prefix on the line holding `marker` (a
// `// @name` comment at the end of an otherwise blank line; the cursor sits
// at column 4, before the comment).
std::vector<std::string> labelsAt(Fixture& f, const std::string& path, const std::string& text,
                                  const std::string& marker)
{
    auto result = CompletionProvider::getCompletion(makeParams(path, lineOf(text, marker), 4),
                                                    f.sdb, text);
    std::vector<std::string> labels;
    if (result.isNull()) return labels;
    for (auto& item : result.get<lsp::Array<lsp::CompletionItem>>())
        labels.emplace_back(item.label);
    return labels;
}

bool has(const std::vector<std::string>& labels, const std::string& name)
{
    return std::find(labels.begin(), labels.end(), name) != labels.end();
}

} // namespace

// ---------------------------------------------------------------------------
// Packages
// ---------------------------------------------------------------------------

// vis_p declares one of everything; vis_q imports it (not exported), vis_r
// imports it and re-exports only vis_pt. Each module imports differently.
const std::string kPkgs =
    "package vis_p;\n"
    "  function int vis_pf(); return 0; endfunction\n"
    "  task vis_pt(); endtask\n"
    "  class VisPC;\n"
    "    int vis_pcfld;\n"
    "    function void vis_pcm(); endfunction\n"
    "  endclass\n"
    "  int vis_pv;\n"
    "  typedef int vis_pint_t;\n"
    "  function void vis_p_user(int vis_puarg);\n"
    "    int vis_puloc;\n"
    "                // @in_p\n"
    "  endfunction\n"
    "endpackage\n"
    "package vis_q;\n"
    "  import vis_p::*;\n"
    "  function int vis_qf(); return 0; endfunction\n"
    "endpackage\n"
    "package vis_r;\n"
    "  import vis_p::*;\n"
    "  export vis_p::vis_pt;\n"
    "  function int vis_rf(); return 0; endfunction\n"
    "endpackage\n"
    "module vis_none;\n"
    "  initial begin\n"
    "                // @m_none\n"
    "  end\n"
    "endmodule\n"
    "module vis_wild;\n"
    "  import vis_p::*;\n"
    "  initial begin\n"
    "                // @m_wild\n"
    "  end\n"
    "endmodule\n"
    "module vis_spec;\n"
    "  import vis_p::vis_pf;\n"
    "  initial begin\n"
    "                // @m_spec\n"
    "  end\n"
    "endmodule\n"
    "module vis_trans;\n"
    "  import vis_q::*;\n"
    "  initial begin\n"
    "                // @m_trans\n"
    "  end\n"
    "endmodule\n"
    "module vis_exp;\n"
    "  import vis_r::*;\n"
    "  initial begin\n"
    "                // @m_exp\n"
    "  end\n"
    "endmodule\n";

TEST_CASE("Completion visibility: inside a package, its own items are offered and other "
          "packages' are not",
          "[completion][visibility][package]")
{
    Fixture f;
    f.ctrl.compile("/vis_pkgs.sv", kPkgs);
    const auto l = labelsAt(f, "/vis_pkgs.sv", kPkgs, "@in_p");
    CHECK(has(l, "vis_pf"));
    CHECK(has(l, "vis_pt"));
    CHECK(has(l, "VisPC"));
    CHECK(has(l, "vis_pv"));
    CHECK(has(l, "vis_pint_t"));
    CHECK(has(l, "vis_puarg"));
    CHECK(has(l, "vis_puloc"));
    // Another package's items, and the package's class's members.
    CHECK_FALSE(has(l, "vis_qf"));
    CHECK_FALSE(has(l, "vis_rf"));
    CHECK_FALSE(has(l, "vis_pcm"));
    CHECK_FALSE(has(l, "vis_pcfld"));
}

TEST_CASE("Completion visibility: a module that imports nothing sees no package's items",
          "[completion][visibility][package]")
{
    Fixture f;
    f.ctrl.compile("/vis_pkgs.sv", kPkgs);
    const auto l = labelsAt(f, "/vis_pkgs.sv", kPkgs, "@m_none");
    for (const char* name : {"vis_pf", "vis_pt", "VisPC", "vis_pv", "vis_pint_t", "vis_qf",
                             "vis_rf", "vis_pcm", "vis_pcfld", "vis_puarg", "vis_puloc"})
        CHECK_FALSE(has(l, name));
}

TEST_CASE("Completion visibility: a wildcard import offers the package's items, not its "
          "class's members or its subroutines' locals",
          "[completion][visibility][package]")
{
    Fixture f;
    f.ctrl.compile("/vis_pkgs.sv", kPkgs);
    const auto l = labelsAt(f, "/vis_pkgs.sv", kPkgs, "@m_wild");
    CHECK(has(l, "vis_pf"));
    CHECK(has(l, "vis_pt"));
    CHECK(has(l, "VisPC"));
    CHECK(has(l, "vis_pv"));
    CHECK_FALSE(has(l, "vis_pcm"));
    CHECK_FALSE(has(l, "vis_pcfld"));
    CHECK_FALSE(has(l, "vis_puarg"));
    CHECK_FALSE(has(l, "vis_puloc"));
    CHECK_FALSE(has(l, "vis_qf"));
}

TEST_CASE("Completion visibility: a specific import offers only that item",
          "[completion][visibility][package]")
{
    Fixture f;
    f.ctrl.compile("/vis_pkgs.sv", kPkgs);
    const auto l = labelsAt(f, "/vis_pkgs.sv", kPkgs, "@m_spec");
    CHECK(has(l, "vis_pf"));
    CHECK_FALSE(has(l, "vis_pt"));
    CHECK_FALSE(has(l, "VisPC"));
    CHECK_FALSE(has(l, "vis_pv"));
}

TEST_CASE("Completion visibility: a plain import is not transitive",
          "[completion][visibility][package]")
{
    Fixture f;
    f.ctrl.compile("/vis_pkgs.sv", kPkgs);
    const auto l = labelsAt(f, "/vis_pkgs.sv", kPkgs, "@m_trans");
    CHECK(has(l, "vis_qf"));
    CHECK_FALSE(has(l, "vis_pf"));
    CHECK_FALSE(has(l, "vis_pt"));
    CHECK_FALSE(has(l, "VisPC"));
}

TEST_CASE("Completion visibility: a specific export re-exports only that item",
          "[completion][visibility][package]")
{
    Fixture f;
    f.ctrl.compile("/vis_pkgs.sv", kPkgs);
    const auto l = labelsAt(f, "/vis_pkgs.sv", kPkgs, "@m_exp");
    CHECK(has(l, "vis_rf"));
    CHECK(has(l, "vis_pt"));
    CHECK_FALSE(has(l, "vis_pf"));
    CHECK_FALSE(has(l, "VisPC"));
}

TEST_CASE("Completion visibility: a package in another file stays out unless imported",
          "[completion][visibility][package]")
{
    Fixture f;
    const std::string pkg =
        "package visx_p;\n"
        "  function int visx_f(); return 0; endfunction\n"
        "  class VisxC; endclass\n"
        "endpackage\n";
    const std::string top =
        "module visx_none;\n"
        "  initial begin\n"
        "                // @none\n"
        "  end\n"
        "endmodule\n"
        "module visx_imp;\n"
        "  import visx_p::*;\n"
        "  initial begin\n"
        "                // @imp\n"
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/visx_p.sv", pkg);
    f.ctrl.compile("/visx_top.sv", top);
    const auto none = labelsAt(f, "/visx_top.sv", top, "@none");
    CHECK_FALSE(has(none, "visx_f"));
    CHECK_FALSE(has(none, "VisxC"));
    const auto imp = labelsAt(f, "/visx_top.sv", top, "@imp");
    CHECK(has(imp, "visx_f"));
    CHECK(has(imp, "VisxC"));
}

// ---------------------------------------------------------------------------
// Import scope: an import belongs to the unit it is written in
// ---------------------------------------------------------------------------

TEST_CASE("Completion visibility: an import inside one module or package doesn't leak into "
          "another unit in the same file",
          "[completion][visibility][package][import-scope]")
{
    Fixture f;
    const std::string text =
        "package visi_p;\n"
        "  function int visi_f(); return 0; endfunction\n"
        "endpackage\n"
        "package visi_q;\n"
        "  import visi_p::*;\n"
        "endpackage\n"
        "module visi_a;\n"
        "  import visi_p::*;\n"
        "endmodule\n"
        "module visi_b;\n"
        "  initial begin\n"
        "                // @b\n"
        "  end\n"
        "endmodule\n"
        "class VisiC;\n"
        "  function void m();\n"
        "                // @cls\n"
        "  endfunction\n"
        "endclass\n";
    f.ctrl.compile("/visi.sv", text);
    CHECK_FALSE(has(labelsAt(f, "/visi.sv", text, "@b"), "visi_f"));
    CHECK_FALSE(has(labelsAt(f, "/visi.sv", text, "@cls"), "visi_f"));
}

TEST_CASE("Completion visibility: an import inside a program or a function applies there",
          "[completion][visibility][package][import-scope]")
{
    Fixture f;
    const std::string text =
        "package visg_p;\n"
        "  function int visg_f(); return 0; endfunction\n"
        "endpackage\n"
        "program visg_prog;\n"
        "  import visg_p::*;\n"
        "  initial begin\n"
        "                // @prog\n"
        "  end\n"
        "endprogram\n"
        "module visg_m;\n"
        "  function void visg_mf();\n"
        "    import visg_p::visg_f;\n"
        "                // @func\n"
        "  endfunction\n"
        "  initial begin\n"
        "                // @mod\n"
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/visg.sv", text);
    CHECK(has(labelsAt(f, "/visg.sv", text, "@prog"), "visg_f"));
    CHECK(has(labelsAt(f, "/visg.sv", text, "@func"), "visg_f"));
    CHECK_FALSE(has(labelsAt(f, "/visg.sv", text, "@mod"), "visg_f"));
}

TEST_CASE("Completion visibility: a compilation-unit import applies to the units after it",
          "[completion][visibility][package][import-scope]")
{
    Fixture f;
    const std::string text =
        "package visu_p;\n"
        "  function int visu_f(); return 0; endfunction\n"
        "endpackage\n"
        "import visu_p::*;\n"
        "module visu_m;\n"
        "  initial begin\n"
        "                // @m\n"
        "  end\n"
        "endmodule\n";
    f.ctrl.compile("/visu.sv", text);
    CHECK(has(labelsAt(f, "/visu.sv", text, "@m"), "visu_f"));
}

// ---------------------------------------------------------------------------
// Classes, modules and subroutines keep their members to themselves
// ---------------------------------------------------------------------------

const std::string kUnits =
    "class VisBase;\n"
    "  int visb_fld;\n"
    "  function void visb_m(); endfunction\n"
    "  task visb_t(); endtask\n"
    "endclass\n"
    "class VisOther;\n"
    "  int viso_fld;\n"
    "  function void viso_m(int viso_arg);\n"
    "    int viso_loc;\n"
    "  endfunction\n"
    "  task viso_t(); endtask\n"
    "  class VisInner;\n"
    "    int visin_fld;\n"
    "    function void visin_m(); endfunction\n"
    "  endclass\n"
    "  function void viso_user();\n"
    "                // @in_outer\n"
    "  endfunction\n"
    "endclass\n"
    "class VisDerived extends VisBase;\n"
    "  function void visd_m();\n"
    "                // @in_derived\n"
    "  endfunction\n"
    "endclass\n"
    "function void vis_unit_f();\n"
    "                // @in_unit_f\n"
    "endfunction\n"
    "task vis_unit_t(); endtask\n"
    "module vis_mod_a;\n"
    "  function void visma_f(int visma_arg); int visma_loc; endfunction\n"
    "  task visma_t(); endtask\n"
    "  class VisMaC; endclass\n"
    "  logic visma_sig;\n"
    "endmodule\n"
    "module vis_mod_b;\n"
    "  function void vismb_f();\n"
    "                // @in_mod_b_f\n"
    "  endfunction\n"
    "  initial begin\n"
    "                // @in_mod_b\n"
    "  end\n"
    "endmodule\n";

TEST_CASE("Completion visibility: inside a class, an unrelated class's members, another "
          "module's subroutines and other subroutines' locals are not offered",
          "[completion][visibility][class]")
{
    Fixture f;
    f.ctrl.compile("/vis_units.sv", kUnits);
    const auto l = labelsAt(f, "/vis_units.sv", kUnits, "@in_derived");
    // Own and inherited members, the compilation unit's subroutines.
    CHECK(has(l, "visd_m"));
    CHECK(has(l, "visb_fld"));
    CHECK(has(l, "visb_m"));
    CHECK(has(l, "visb_t"));
    CHECK(has(l, "vis_unit_f"));
    CHECK(has(l, "vis_unit_t"));
    // An unrelated class's members, arguments and locals.
    for (const char* name : {"viso_fld", "viso_m", "viso_t", "viso_arg", "viso_loc",
                             "viso_user", "visin_fld", "visin_m", "VisInner"})
        CHECK_FALSE(has(l, name));
    // A module's own declarations.
    for (const char* name : {"visma_f", "visma_t", "VisMaC", "visma_sig", "visma_arg",
                             "visma_loc", "vismb_f"})
        CHECK_FALSE(has(l, name));
}

TEST_CASE("Completion visibility: at compilation-unit scope, no class member or module "
          "declaration is offered",
          "[completion][visibility][global]")
{
    Fixture f;
    f.ctrl.compile("/vis_units.sv", kUnits);
    const auto l = labelsAt(f, "/vis_units.sv", kUnits, "@in_unit_f");
    // Top-level declarations are.
    CHECK(has(l, "VisBase"));
    CHECK(has(l, "VisOther"));
    CHECK(has(l, "vis_unit_t"));
    CHECK(has(l, "vis_mod_a"));
    for (const char* name : {"visb_fld", "visb_m", "visb_t", "viso_fld", "viso_m", "viso_t",
                             "viso_arg", "viso_loc", "VisInner", "visin_m", "visd_m",
                             "visma_f", "visma_t", "VisMaC", "visma_sig", "vismb_f"})
        CHECK_FALSE(has(l, name));
}

TEST_CASE("Completion visibility: inside a module, class members and another module's "
          "declarations are not offered",
          "[completion][visibility][module]")
{
    Fixture f;
    f.ctrl.compile("/vis_units.sv", kUnits);
    for (const char* marker : {"@in_mod_b", "@in_mod_b_f"}) {
        INFO(marker);
        const auto l = labelsAt(f, "/vis_units.sv", kUnits, marker);
        CHECK(has(l, "vismb_f"));
        CHECK(has(l, "vis_unit_f"));
        for (const char* name : {"visb_fld", "visb_m", "visb_t", "viso_m", "viso_t", "VisInner",
                                 "visma_f", "visma_t", "VisMaC", "visma_sig", "visma_arg",
                                 "visma_loc"})
            CHECK_FALSE(has(l, name));
    }
}

TEST_CASE("Completion visibility: a nested class is a bare name inside its outer class only, "
          "and its members never are",
          "[completion][visibility][class][nested]")
{
    Fixture f;
    f.ctrl.compile("/vis_units.sv", kUnits);
    const auto outer = labelsAt(f, "/vis_units.sv", kUnits, "@in_outer");
    CHECK(has(outer, "VisInner"));
    CHECK(has(outer, "viso_m"));
    CHECK_FALSE(has(outer, "visin_fld"));
    CHECK_FALSE(has(outer, "visin_m"));
    CHECK_FALSE(has(outer, "viso_arg")); // viso_m's argument, not viso_user's
    CHECK_FALSE(has(outer, "viso_loc"));
    CHECK_FALSE(has(labelsAt(f, "/vis_units.sv", kUnits, "@in_mod_b"), "VisInner"));
}

// ---------------------------------------------------------------------------
// Where a class, function or task may be declared (keyword legality)
// ---------------------------------------------------------------------------

TEST_CASE("Completion visibility: `class` is offered where a class may be declared, and not "
          "in a function or task body",
          "[completion][visibility][keyword]")
{
    Fixture f;
    const std::string text =
        "                // @top\n"
        "package visk_p;\n"
        "                // @pkg\n"
        "endpackage\n"
        "module visk_m;\n"
        "                // @mod\n"
        "  function void visk_mf();\n"
        "                // @mod_func\n"
        "  endfunction\n"
        "endmodule\n"
        "interface visk_if;\n"
        "                // @ifc\n"
        "endinterface\n"
        "class ViskC;\n"
        "                // @cls\n"
        "  function void visk_cf();\n"
        "                // @cls_func\n"
        "  endfunction\n"
        "  task visk_ct();\n"
        "                // @cls_task\n"
        "  endtask\n"
        "endclass\n";
    f.ctrl.compile("/visk.sv", text);
    for (const char* marker : {"@top", "@pkg", "@mod", "@ifc", "@cls"}) {
        INFO(marker);
        CHECK(has(labelsAt(f, "/visk.sv", text, marker), "class"));
    }
    for (const char* marker : {"@mod_func", "@cls_func", "@cls_task"}) {
        INFO(marker);
        const auto l = labelsAt(f, "/visk.sv", text, marker);
        CHECK_FALSE(has(l, "class"));
        CHECK_FALSE(has(l, "endclass"));
        // No nested subroutines (13.3) or design units.
        CHECK_FALSE(has(l, "function"));
        CHECK_FALSE(has(l, "task"));
        CHECK_FALSE(has(l, "module"));
        CHECK_FALSE(has(l, "package"));
    }
    // A class body may declare subroutines and nested classes, not design
    // units; `endclass` only closes a class.
    const auto cls = labelsAt(f, "/visk.sv", text, "@cls");
    CHECK(has(cls, "function"));
    CHECK(has(cls, "task"));
    CHECK(has(cls, "endclass"));
    CHECK_FALSE(has(cls, "module"));
    CHECK_FALSE(has(cls, "package"));
    CHECK_FALSE(has(cls, "endmodule"));
    CHECK_FALSE(has(labelsAt(f, "/visk.sv", text, "@mod"), "endclass"));
    CHECK_FALSE(has(labelsAt(f, "/visk.sv", text, "@pkg"), "module"));
}

// ---------------------------------------------------------------------------
// Block scopes (plan.md §6.30, "block scopes"): a block's locals and a loop's
// variables are bare names inside it only; blocks themselves are never
// offered.
// ---------------------------------------------------------------------------

TEST_CASE("Completion visibility: a block's locals stay in the block", "[completion][visibility][block]")
{
    Fixture f;
    const std::string text =
        "module visb_m;\n"
        "  int visb_mod;\n"
        "  initial begin : visb_named\n"
        "    int visb_a;\n"
        "                // @in_a\n"
        "  end\n"
        "  initial begin\n"
        "    int visb_b;\n"
        "    for (int visb_i = 0; visb_i < 2; visb_i++) begin\n"
        "                // @in_loop\n"
        "    end\n"
        "                // @after_loop\n"
        "  end\n"
        "                // @mod\n"
        "endmodule\n";
    f.ctrl.compile("/visb.sv", text);

    const auto inA = labelsAt(f, "/visb.sv", text, "@in_a");
    CHECK(has(inA, "visb_a"));
    CHECK(has(inA, "visb_mod"));
    CHECK_FALSE(has(inA, "visb_b"));
    CHECK_FALSE(has(inA, "visb_i"));

    const auto inLoop = labelsAt(f, "/visb.sv", text, "@in_loop");
    CHECK(has(inLoop, "visb_i"));
    CHECK(has(inLoop, "visb_b"));
    CHECK_FALSE(has(inLoop, "visb_a"));

    const auto after = labelsAt(f, "/visb.sv", text, "@after_loop");
    CHECK(has(after, "visb_b"));
    CHECK_FALSE(has(after, "visb_i"));

    const auto mod = labelsAt(f, "/visb.sv", text, "@mod");
    CHECK(has(mod, "visb_mod"));
    for (const char* name : {"visb_a", "visb_b", "visb_i"}) {
        INFO(name);
        CHECK_FALSE(has(mod, name));
    }
    // A block is not a completion candidate, named or not.
    for (const auto* labels : {&inA, &inLoop, &after, &mod}) {
        CHECK_FALSE(has(*labels, "visb_named"));
        for (const auto& l : *labels) CHECK(l.rfind('@', 0) != 0);
    }
}

TEST_CASE("Completion visibility: keywords inside a block follow the enclosing unit",
          "[completion][visibility][block]")
{
    Fixture f;
    const std::string text =
        "class VisbK;\n"
        "  function void visbk_f();\n"
        "    begin\n"
        "      int visbk_t;\n"
        "                // @in_block\n"
        "    end\n"
        "  endfunction\n"
        "endclass\n";
    f.ctrl.compile("/visbk.sv", text);
    const auto l = labelsAt(f, "/visbk.sv", text, "@in_block");
    CHECK(has(l, "visbk_t"));
    CHECK_FALSE(has(l, "function"));
    CHECK_FALSE(has(l, "class"));
    CHECK_FALSE(has(l, "endclass"));
}
