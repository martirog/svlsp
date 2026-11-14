// The import check (plan.md §6.37): `import p::x;` / `export p::x;` whose
// package doesn't declare (or export) x, or `import p::...` of a package no
// DB declares, is an 'import' Warning over the name. Driven through a real
// CompilationController::compile under a project config, set up as the
// server does it (checkTypeRefs as the TypeChecker turns the check on).

#include <catch2/catch_test_macros.hpp>
#include "lsp/symbol_resolution.h"
#include "lsp/library_db_builder.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct F {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController ctrl{sdb};
    ProjectConfig         config;

    F()
    {
        db.initSchema();
        ctrl.setTypeChecker(checkTypeRefs);
    }

    // compile()'s diagnostics on the primary whose message starts with one
    // of the import check's.
    std::vector<ParseError> compile(const std::string& path, const std::string& text,
                                    std::vector<std::string>* refreshed = nullptr)
    {
        std::vector<ParseError> out;
        for (auto& e : ctrl.compile(path, text, &config, nullptr, false, refreshed))
            if (isImportMessage(e.message)) out.push_back(e);
        return out;
    }

    static bool isImportMessage(const std::string& m)
    {
        return m.rfind("unknown package", 0) == 0 || m.find("is not declared in package") !=
                                                         std::string::npos;
    }

    std::vector<SubjectDiagnosticRow> rows() { return sdb.diagnosticsFromSource("import"); }
};

void writeFile(const std::string& path, const std::string& content)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path) << content;
}

} // namespace

TEST_CASE("imports: a name the package doesn't declare is a Warning over the name",
          "[importcheck][phase6.37]")
{
    F f;
    f.compile("/ic1/pkg.sv",
              "package ic1_p;\n"
              "  class Real; endclass\n"
              "  typedef policy_t policy_q[$];\n" // the reversed typedef: declares policy_q
              "endpackage\n");
    const auto warnings = f.compile("/ic1/use.sv",
                                    "module ic1_m;\n"
                                    "  import ic1_p::Real;\n"
                                    "  import ic1_p::policy_t;\n"
                                    "  import ic1_p::*;\n"
                                    "endmodule\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].line == 3);
    CHECK(warnings[0].column == 16);
    CHECK(warnings[0].endColumn == 24);
    CHECK(warnings[0].severity == 2);
    CHECK(warnings[0].message == "'policy_t' is not declared in package 'ic1_p'");
    const auto rows = f.rows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].filePath == "/ic1/use.sv");
    CHECK(rows[0].subject == "ic1_p::policy_t");
}

TEST_CASE("imports: an unknown package is a Warning over the package name",
          "[importcheck][phase6.37]")
{
    F f;
    const auto warnings = f.compile("/ic2/use.sv",
                                    "module ic2_m;\n"
                                    "  import ic2_nopkg::*;\n"
                                    "  import ic2_nopkg2::X;\n"
                                    "  import std::*;\n" // built in
                                    "endmodule\n");
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].line == 2);
    CHECK(warnings[0].column == 9);
    CHECK(warnings[0].endColumn == 18);
    CHECK(warnings[0].message == "unknown package 'ic2_nopkg'");
    CHECK(warnings[1].line == 3);
    CHECK(warnings[1].column == 9);
    CHECK(warnings[1].message == "unknown package 'ic2_nopkg2'");
}

TEST_CASE("imports: the message names the packages that do declare the name",
          "[importcheck][phase6.37]")
{
    F f;
    f.compile("/ic3/pkgs.sv",
              "package ic3_a; endpackage\n"
              "package ic3_b; typedef int moved_t; endpackage\n");
    const auto warnings =
        f.compile("/ic3/use.sv", "module ic3_m;\n  import ic3_a::moved_t;\nendmodule\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message ==
          "'moved_t' is not declared in package 'ic3_a' (declared in ic3_b)");
}

TEST_CASE("imports: every kind of package item can be imported", "[importcheck][phase6.37]")
{
    F f;
    f.compile("/ic4/pkg.sv",
              "package ic4_p;\n"
              "  class C; endclass\n"
              "  typedef class Fwd;\n"
              "  typedef int my_t;\n"
              "  typedef enum {RED, GREEN} color_t;\n"
              "  enum {A1, A2} anon_e;\n"
              "  typedef struct { int a; } s_t;\n"
              "  parameter int P = 1;\n"
              "  localparam int LP = 2;\n"
              "  parameter type PT = int;\n"
              "  int v;\n"
              "  const int CV = 3;\n"
              "  function void f(); endfunction\n"
              "  task t(); endtask\n"
              "  import \"DPI-C\" function int dpi_f(int x);\n"
              "  covergroup cg; endgroup\n"
              "  let my_let(x) = x + 1;\n"
              "  sequence seq1; 1; endsequence\n"
              "  property prop1; 1; endproperty\n"
              "  nettype logic nt_t;\n"
              "  checker chk; endchecker\n"
              "  interface class IC; endclass\n"
              "  virtual class VC; endclass\n"
              "  event ev;\n"
              "  class Fwd; endclass\n"
              "endpackage\n");
    std::string use = "module ic4_m;\n";
    for (const char* name : {"C", "Fwd", "my_t", "RED", "color_t", "anon_e", "A2", "s_t", "P",
                             "LP", "PT", "v", "CV", "f", "t", "dpi_f", "cg", "my_let", "seq1",
                             "prop1", "nt_t", "chk", "IC", "VC", "ev"})
        use += std::string{"  import ic4_p::"} + name + ";\n";
    use += "endmodule\n";
    CHECK(f.compile("/ic4/use.sv", use).empty());
    CHECK(f.rows().empty());
}

TEST_CASE("imports: a name a package exports is importable from it", "[importcheck][phase6.37]")
{
    F f;
    f.compile("/ic5/pkgs.sv",
              "package ic5_base; class B; endclass typedef int t1; typedef int t2; endpackage\n"
              "package ic5_one; export ic5_base::B; endpackage\n"
              "package ic5_all; import ic5_base::*; export ic5_base::*; endpackage\n"
              "package ic5_any; import ic5_base::t2; export *::*; endpackage\n"
              "package ic5_chain; export ic5_all::*; endpackage\n");
    CHECK(f.compile("/ic5/use.sv",
                    "module ic5_m;\n"
                    "  import ic5_one::B;\n"
                    "  import ic5_all::t1;\n"
                    "  import ic5_any::t2;\n"
                    "  import ic5_chain::t1;\n"
                    "endmodule\n")
              .empty());
    // An export is checked like an import; `export *::*` itself never is.
    const auto warnings =
        f.compile("/ic5/bad.sv", "package ic5_bad;\n  export ic5_base::nope;\nendpackage\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message == "'nope' is not declared in package 'ic5_base'");
    CHECK(f.compile("/ic5/use.sv", "module ic5_m;\n  import ic5_one::t1;\nendmodule\n").size() ==
          1);
}

TEST_CASE("imports: a package from an attached library DB is known, its exports followed", "[importcheck][phase6.37]")
{
    const std::string root = (fs::temp_directory_path() / "svlsp_test_import_check").string();
    writeFile(root + "/lib/libp.sv",
              "package ic6_base;\n  typedef int base_t;\nendpackage\n"
              "package ic6_libp;\n  import ic6_base::*;\n  export ic6_base::*;\n"
              "  class LibC; endclass\nendpackage\n");
    writeFile(root + "/lib/lib.f", "libp.sv\n");
    const std::string libDb = root + "/lib/lib.db";
    fs::remove(libDb);
    REQUIRE(LibraryDbBuilder::build(root + "/lib/lib.f", libDb).ok);

    F f;
    f.config.libraryDbs = {libDb};
    f.sdb.attachLibraryDbs(f.config.libraryDbs);
    const auto warnings = f.compile(root + "/top.sv",
                                    "module ic6_top;\n"
                                    "  import ic6_libp::*;\n"
                                    "  import ic6_libp::LibC;\n"
                                    "  import ic6_libp::NoSuch;\n"
                                    "  import ic6_libp::base_t;\n" // re-exported in the library
                                    "endmodule\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].line == 4);
}

TEST_CASE("imports: no check without a project config", "[importcheck][phase6.37]")
{
    F f;
    const auto diags = f.ctrl.compile("/ic7/use.sv", "module ic7_m;\n  import ic7_no::X;\nendmodule\n",
                                      nullptr);
    CHECK(std::none_of(diags.begin(), diags.end(),
                       [](const ParseError& e) { return F::isImportMessage(e.message); }));
    CHECK(f.rows().empty());
}

TEST_CASE("imports: a warning clears once the package declares the name",
          "[importcheck][phase6.37]")
{
    F f;
    const std::string use = "module ic8_m;\n  import ic8_p::late_t;\nendmodule\n";
    f.compile("/ic8/pkg.sv", "package ic8_p;\nendpackage\n");
    REQUIRE(f.compile("/ic8/use.sv", use).size() == 1);

    std::vector<std::string> refreshed;
    f.compile("/ic8/pkg.sv", "package ic8_p;\n  typedef int late_t;\nendpackage\n", &refreshed);
    CHECK(f.rows().empty());
    CHECK(std::find(refreshed.begin(), refreshed.end(), "/ic8/use.sv") != refreshed.end());
}

TEST_CASE("imports: a package compiled after its importer clears the warning",
          "[importcheck][phase6.37]")
{
    F f;
    REQUIRE(f.compile("/ic9/use.sv", "module ic9_m;\n  import ic9_p::*;\nendmodule\n").size() == 1);
    std::vector<std::string> refreshed;
    f.compile("/ic9/pkg.sv", "package ic9_p;\nendpackage\n", &refreshed);
    CHECK(f.rows().empty());
    CHECK(std::find(refreshed.begin(), refreshed.end(), "/ic9/use.sv") != refreshed.end());
}

TEST_CASE("imports: an import in an `include'd file is reported on that file",
          "[importcheck][phase6.37]")
{
    const std::string root = (fs::temp_directory_path() / "svlsp_test_import_check_inc").string();
    writeFile(root + "/body.svh", "  import ic10_p::missing_t;\n");
    F f;
    f.compile(root + "/pkg.sv", "package ic10_p;\nendpackage\n");
    f.compile(root + "/top.sv", "module ic10_m;\n`include \"body.svh\"\nendmodule\n");
    const auto rows = f.rows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].filePath == root + "/body.svh");
    CHECK(rows[0].line == 1);
    CHECK(rows[0].col == 17);
}
