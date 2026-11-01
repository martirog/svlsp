// The declared-type check (plan.md §6.21 step 3): a type name written at a
// declaration that no visible declaration covers is a 'reference' Warning.
// Driven through a real CompilationController::compile under a project
// config, with checkTypeRefs as its TypeChecker, as the server sets it up.

#include <catch2/catch_test_macros.hpp>
#include "lsp/symbol_resolution.h"
#include "lsp/library_db_builder.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "compiler/project_config.h"
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

    // compile()'s Warnings (the primary file's 'reference' diagnostics).
    std::vector<ParseError> compile(const std::string& path, const std::string& text,
                                    std::vector<std::string>* refreshed = nullptr)
    {
        std::vector<ParseError> warnings;
        for (auto& e : ctrl.compile(path, text, &config, nullptr, false, refreshed))
            if (e.severity == 2) warnings.push_back(e);
        return warnings;
    }

    std::vector<SubjectDiagnosticRow> rows() { return sdb.diagnosticsFromSource("reference"); }
};

bool mentions(const std::vector<ParseError>& diags, const std::string& text)
{
    for (const auto& d : diags)
        if (d.message.find(text) != std::string::npos) return true;
    return false;
}

void writeFile(const std::string& path, const std::string& content)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path) << content;
}

} // namespace

TEST_CASE("type refs: an imported class is visible; a missing one is a Warning at its name",
          "[typecheck][phase6.21]")
{
    F f;
    const std::string pkg =
        "package tr1_p;\n"
        "  class RealClass; endclass\n"
        "endpackage\n";
    const std::string use =
        "module tr1_m;\n"
        "  import tr1_p::RealClass;\n"
        "  RealClass obj;\n"
        "endmodule\n";
    f.compile("/tr1/pkg.sv", pkg);
    CHECK(f.compile("/tr1/use.sv", use).empty());
    CHECK(f.rows().empty());

    // RealClass removed from the package; use.sv recompiles.
    f.compile("/tr1/pkg.sv", "package tr1_p;\nendpackage\n");
    const auto warnings = f.compile("/tr1/use.sv", use + "\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].line == 3);
    CHECK(warnings[0].column == 2);
    CHECK(warnings[0].message == "unknown type 'RealClass'");
    const auto rows = f.rows();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].filePath == "/tr1/use.sv");
    CHECK(rows[0].subject == "RealClass");
}

TEST_CASE("type refs: a class that exists but isn't imported is not visible",
          "[typecheck][phase6.21]")
{
    F f;
    f.compile("/tr3/pkg.sv", "package tr3_pb;\n  class OnlyB; endclass\nendpackage\n");
    const auto warnings = f.compile("/tr3/m.sv", "module tr3_m;\n  OnlyB o;\nendmodule\n");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].message ==
          "type 'OnlyB' is not visible here (declared in tr3_pb; missing import?)");
    CHECK(f.compile("/tr3/m.sv", "module tr3_m;\n  import tr3_pb::*;\n  OnlyB o;\nendmodule\n")
              .empty());
    CHECK(f.rows().empty());
}

TEST_CASE("type refs: a class from an attached library DB is visible", "[typecheck][phase6.21]")
{
    const std::string root = (fs::temp_directory_path() / "svlsp_test_type_ref_check").string();
    writeFile(root + "/lib/libp.sv", "package tr4_libp;\n  class LibC; endclass\nendpackage\n");
    writeFile(root + "/lib/lib.f", "libp.sv\n");
    const std::string libDb = root + "/lib/lib.db";
    fs::remove(libDb);
    REQUIRE(LibraryDbBuilder::build(root + "/lib/lib.f", libDb).ok);

    F f;
    f.config.libraryDbs = {libDb};
    f.sdb.attachLibraryDbs(f.config.libraryDbs);
    CHECK(f.compile(root + "/top.sv",
                    "module tr4_top;\n  import tr4_libp::*;\n  LibC c;\n  tr4_libp::LibC d;\n"
                    "endmodule\n")
              .empty());
}

TEST_CASE("type refs: qualified names", "[typecheck][phase6.21]")
{
    F f;
    const auto warnings = f.compile("/tr5/q.sv",
        "package tr5_p;\n"
        "  class C; typedef int inner_t; endclass\n"
        "endpackage\n"
        "class Tr5Box #(type T = int);\n"
        "  T::type_id tid;\n"                 // 5: a type parameter's member: unknown here
        "endclass\n"
        "module tr5_m;\n"
        "  tr5_p::C a;\n"                     // 8
        "  tr5_p::C::inner_t b;\n"            // 9
        "  tr5_p::Missing c;\n"               // 10
        "  tr5_nopkg::C d;\n"                 // 11
        "  std::process e;\n"                 // 12
        "endmodule\n");
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].line == 10);
    CHECK(warnings[0].message == "unknown type 'Missing' in 'tr5_p'");
    CHECK(warnings[1].line == 11);
    CHECK(warnings[1].message == "unknown package or class 'tr5_nopkg'");
}

TEST_CASE("type refs: every kind of type declaration counts", "[typecheck][phase6.21]")
{
    F f;
    const auto warnings = f.compile("/tr6/k.sv",
        "interface tr6_if; endinterface\n"
        "package tr6_p;\n"
        "  typedef enum {A, B} e_t;\n"
        "  typedef struct { int x; } s_t;\n"
        "  typedef class Fw;\n"
        "  class Base #(type BT = int); typedef int base_t; endclass\n"
        "  class Der extends Base;\n"
        "    base_t a;\n"           // inherited typedef
        "    BT bt;\n"              // inherited type parameter
        "    Base::base_t b;\n"     // through the class
        "    Fw fw;\n"              // forward typedef
        "  endclass\n"
        "  class Fw; endclass\n"
        "  class Box #(type T = int); T t; e_t e; s_t s; endclass\n"
        "endpackage\n"
        "module tr6_m;\n"
        "  logic clk, v;\n"
        "  covergroup cg_t @(posedge clk); coverpoint v; endgroup\n"
        "  cg_t cg = new;\n"
        "  virtual tr6_if vif;\n"
        "  process p; mailbox mb; semaphore sem;\n"
        "endmodule\n");
    for (const auto& w : warnings) INFO(w.line << ": " << w.message);
    CHECK(warnings.empty());
}

TEST_CASE("type refs: a type a macro expanded to isn't checked", "[typecheck][phase6.21]")
{
    F f;
    CHECK(f.compile("/tr7/m.sv",
                    "`define TR7_DECL(n) Tr7NoSuch n;\n"
                    "module tr7_m;\n"
                    "  `TR7_DECL(x)\n"
                    "endmodule\n")
              .empty());
    // Nor one written as a macro argument: its recorded column is the
    // expansion's, not the argument's (disclosed in plan.md §6.21 step 3).
    CHECK(f.compile("/tr7/a.sv",
                    "`define TR7_VAR(t, n) t n;\n"
                    "module tr7_a;\n"
                    "  `TR7_VAR(Tr7Arg, y)\n"
                    "endmodule\n")
              .empty());
}

TEST_CASE("type refs: no check without a project config", "[typecheck][phase6.21]")
{
    F f;
    f.ctrl.compile("/tr8/m.sv", "module tr8_m;\n  Tr8Missing x;\nendmodule\n");
    CHECK(f.rows().empty());
}

TEST_CASE("type refs: a warning clears once another file declares the type",
          "[typecheck][phase6.21]")
{
    F f;
    std::vector<std::string> refreshed;
    REQUIRE(f.compile("/tr9/b.sv",
                      "module tr9_b;\n  import tr9_p::*;\n  Tr9X x;\nendmodule\n", &refreshed)
                .size() == 1);
    CHECK(refreshed.empty());
    f.compile("/tr9/a.sv", "package tr9_p;\n  class Tr9X; endclass\nendpackage\n", &refreshed);
    CHECK(f.rows().empty());
    CHECK(refreshed == std::vector<std::string>{"/tr9/b.sv"});
}

TEST_CASE("type refs: a class used before its declaration needs a forward typedef",
          "[typecheck][phase6.21]")
{
    F f;
    const auto late = f.compile("/tr10/a.sv",
        "package tr10_p;\n"
        "  class A; B b; endclass\n"
        "  class B; endclass\n"
        "endpackage\n");
    REQUIRE(late.size() == 1);
    CHECK(late[0].message.find("is not visible here") != std::string::npos);
    CHECK(f.compile("/tr10/a.sv",
                    "package tr10_p;\n"
                    "  typedef class B;\n"
                    "  class A; B b; endclass\n"
                    "  class B; endclass\n"
                    "endpackage\n")
              .empty());
    CHECK(f.rows().empty());
}

TEST_CASE("type refs: recompiling with the type fixed clears its warning",
          "[typecheck][phase6.21]")
{
    F f;
    f.compile("/tr11/pkg.sv", "package tr11_p;\n  class Tr11Item; endclass\nendpackage\n");
    REQUIRE(f.compile("/tr11/use.sv",
                      "module tr11_m;\n  import tr11_p::*;\n  Tr11Missing x;\nendmodule\n")
                .size() == 1);
    CHECK(f.compile("/tr11/use.sv",
                    "module tr11_m;\n  import tr11_p::*;\n  Tr11Item x;\nendmodule\n")
              .empty());
    CHECK(f.rows().empty());
    CHECK(f.sdb.diagnosticsForFile("/tr11/use.sv").empty());
}
