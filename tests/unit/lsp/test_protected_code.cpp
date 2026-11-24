// Protected (encrypted) code (plan.md §6.41): an envelope is skipped, its
// scope recorded, and a name that doesn't resolve while protected code is
// in reach is an Information note instead of a Warning (or nothing, with
// notes off). Driven through a real CompilationController under a project
// config, with checkTypeRefs, as the server sets it up.

#include <catch2/catch_test_macros.hpp>
#include "lsp/symbol_resolution.h"
#include "lsp/library_db_builder.h"
#include "db/database.h"
#include "db/library_resolver.h"
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

const std::string kEnvelope =
    "`pragma protect begin_protected\n"
    "`pragma protect encoding=(enctype=\"base64\", bytes=8)\n"
    "`pragma protect data_block\n"
    "aGVsbG8gd29ybGQ=\n"
    "`pragma protect end_protected\n";

struct P {
    Database              db{":memory:"};
    SymbolDatabase        sdb{db};
    CompilationController ctrl{sdb};
    ProjectConfig         config;

    P()
    {
        db.initSchema();
        ctrl.setTypeChecker(checkTypeRefs);
    }
    void compile(const std::string& path, const std::string& text)
    {
        ctrl.compile(path, text, &config);
    }
    // `path`'s diagnostics of `source`, as "line:severity:message".
    std::vector<std::string> diags(const std::string& path, const std::string& source)
    {
        std::vector<std::string> out;
        for (const auto& d : sdb.diagnosticsForFile(path))
            if (d.source == source)
                out.push_back(std::to_string(d.line) + ":" + std::to_string(d.severity) + ":" +
                              d.message);
        return out;
    }
    std::vector<std::string> regions()
    {
        std::vector<std::string> out;
        for (const auto& r : sdb.protectedRegions())
            out.push_back(fs::path(r.filePath).filename().string() + ":" +
                          std::to_string(r.line) + "-" + std::to_string(r.endLine) + "@" +
                          r.scope);
        return out;
    }
};

void writeFile(const std::string& path, const std::string& content)
{
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path) << content;
}

std::string note(const std::string& message, const std::string& where)
{
    return message + "; it may be declared in protected code (" + where + ")";
}

} // namespace

TEST_CASE("protected: an envelope is no syntax error, and its scope is recorded",
          "[protected][phase6.41]")
{
    P f;
    f.compile("/pr1/top.sv", kEnvelope + "module pr1_m;\n" + kEnvelope + "endmodule\n");
    f.compile("/pr1/pkg.sv",
              "package pr1_p;\n" + kEnvelope + "  class pr1_c;\n" + kEnvelope +
                  "  endclass\nendpackage\n");
    CHECK(f.diags("/pr1/top.sv", "compile").empty());
    CHECK(f.diags("/pr1/pkg.sv", "compile").empty());
    CHECK(f.regions() == std::vector<std::string>{"pkg.sv:2-6@pr1_p", "pkg.sv:8-12@pr1_p::pr1_c",
                                                  "top.sv:1-5@", "top.sv:7-11@pr1_m"});
    // Recompiled without them, the file's regions go.
    f.compile("/pr1/top.sv", "module pr1_m;\nendmodule\n");
    CHECK(f.regions() == std::vector<std::string>{"pkg.sv:2-6@pr1_p", "pkg.sv:8-12@pr1_p::pr1_c"});
}

TEST_CASE("protected: an envelope without an end is an error", "[protected][phase6.41]")
{
    P f;
    f.compile("/pr2/a.sv", "module pr2_m;\n`pragma protect begin_protected\nabc\n");
    const auto errs = f.diags("/pr2/a.sv", "preprocess");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0] == "2:1:protected envelope has no end (`pragma protect end_protected)");
}

TEST_CASE("protected: an unresolved type with protected code in reach is a note",
          "[protected][phase6.41]")
{
    P f;
    f.compile("/pr3/ip.sv", "package pr3_ip;\n" + kEnvelope + "endpackage\n");
    f.compile("/pr3/other.sv", "package pr3_other;\n" + kEnvelope + "endpackage\n");
    f.compile("/pr3/mod.sv", "module pr3_mod;\n" + kEnvelope + "  pr3_a a;\nendmodule\n");
    f.compile("/pr3/use.sv",
              "module pr3_use;\n"
              "  import pr3_ip::*;\n"
              "  pr3_b b;\n"              // 3: pr3_ip is imported
              "  pr3_ip::pr3_c c;\n"      // 4: qualified, into pr3_ip
              "  pr3_other::pr3_d d;\n"   // 5: qualified, into pr3_other
              "endmodule\n"
              "module pr3_use2;\n"
              "  pr3_e e;\n"              // 8: nothing in reach
              "  pr3_none::pr3_f g;\n"    // 9: unknown package, no unit-level region
              "endmodule\n");
    CHECK(f.diags("/pr3/mod.sv", "reference") ==
          std::vector<std::string>{"7:3:" + note("unknown type 'pr3_a'", "mod.sv:2")});
    CHECK(f.diags("/pr3/use.sv", "reference") ==
          std::vector<std::string>{
              "3:3:" + note("unknown type 'pr3_b'", "ip.sv:2"),
              "4:3:" + note("unknown type 'pr3_c' in 'pr3_ip'", "ip.sv:2"),
              "5:3:" + note("unknown type 'pr3_d' in 'pr3_other'", "other.sv:2"),
              "8:2:unknown type 'pr3_e'",
              "9:2:unknown package or class 'pr3_none'"});
}

TEST_CASE("protected: a region at compilation-unit level reaches every unresolved type",
          "[protected][phase6.41]")
{
    P f;
    f.compile("/pr4/vendor.sv", kEnvelope);
    f.compile("/pr4/use.sv",
              "module pr4_use;\n  pr4_a a;\n  pr4_pkg::pr4_b b;\nendmodule\n");
    CHECK(f.diags("/pr4/use.sv", "reference") ==
          std::vector<std::string>{
              "2:3:" + note("unknown type 'pr4_a'", "vendor.sv:1"),
              "3:3:" + note("unknown package or class 'pr4_pkg'", "vendor.sv:1")});
}

TEST_CASE("protected: imports and instantiations", "[protected][phase6.41]")
{
    P f;
    f.compile("/pr5/ip.sv", "package pr5_ip;\n" + kEnvelope + "endpackage\n");
    f.compile("/pr5/use.sv",
              "module pr5_use;\n"
              "  import pr5_ip::pr5_x;\n"   // 2: pr5_ip may declare it
              "  import pr5_gone::*;\n"     // 3: unknown package, no unit-level region
              "  pr5_cell u0();\n"          // 4: unknown module
              "endmodule\n");
    LibraryResolver::resolve(f.config, f.ctrl, f.sdb);
    CHECK(f.diags("/pr5/use.sv", "import") ==
          std::vector<std::string>{
              "2:3:" + note("'pr5_x' is not declared in package 'pr5_ip'", "ip.sv:2"),
              "3:2:unknown package 'pr5_gone'"});
    REQUIRE(f.diags("/pr5/use.sv", "library").size() == 1);
    CHECK(f.diags("/pr5/use.sv", "library")[0].rfind("4:1:Unresolved instantiation", 0) == 0);

    // A whole-file envelope may hide the package and the module.
    f.compile("/pr5/vendor.sv", kEnvelope);
    f.compile("/pr5/use.sv",
              "module pr5_use;\n  import pr5_gone::*;\n  pr5_cell u0();\nendmodule\n");
    LibraryResolver::resolve(f.config, f.ctrl, f.sdb);
    CHECK(f.diags("/pr5/use.sv", "import") ==
          std::vector<std::string>{"2:3:" + note("unknown package 'pr5_gone'", "vendor.sv:1")});
    const auto lib = f.diags("/pr5/use.sv", "library");
    REQUIRE(lib.size() == 1);
    CHECK(lib[0].rfind("3:3:Unresolved instantiation of 'pr5_cell'", 0) == 0);
    CHECK(lib[0].find("protected code (vendor.sv:1)") != std::string::npos);
}

TEST_CASE("protected: a warning written before the protected code was compiled becomes a note",
          "[protected][phase6.41]")
{
    P f;
    f.compile("/pr6/use.sv", "module pr6_use;\n  import pr6_ip::*;\n  pr6_t t;\nendmodule\n");
    CHECK(f.diags("/pr6/use.sv", "reference") ==
          std::vector<std::string>{"3:2:unknown type 'pr6_t'"});
    f.compile("/pr6/ip.sv", "package pr6_ip;\n" + kEnvelope + "endpackage\n");
    CHECK(f.diags("/pr6/use.sv", "reference") ==
          std::vector<std::string>{"3:3:" + note("unknown type 'pr6_t'", "ip.sv:2")});
}

TEST_CASE("protected: with notes off, no diagnostic", "[protected][phase6.41]")
{
    P f;
    f.sdb.setProtectedNotes(false);
    f.compile("/pr7/vendor.sv", kEnvelope);
    f.compile("/pr7/use.sv",
              "module pr7_use;\n  import pr7_gone::*;\n  pr7_a a;\n  pr7_cell u0();\nendmodule\n");
    LibraryResolver::resolve(f.config, f.ctrl, f.sdb);
    CHECK(f.diags("/pr7/use.sv", "reference").empty());
    CHECK(f.diags("/pr7/use.sv", "import").empty());
    CHECK(f.diags("/pr7/use.sv", "library").empty());
}

TEST_CASE("protected: a library DB built from protected code reaches the project",
          "[protected][phase6.41]")
{
    const std::string root = (fs::temp_directory_path() / "svlsp_test_protected").string();
    fs::remove_all(root);
    writeFile(root + "/lib/ip.sv", "package pr8_ip;\n" + kEnvelope + "endpackage\n");
    writeFile(root + "/lib/lib.f", "ip.sv\n");
    const std::string libDb = root + "/lib/lib.db";
    REQUIRE(LibraryDbBuilder::build(root + "/lib/lib.f", libDb).ok);

    P f;
    f.config.libraryDbs = {libDb};
    f.sdb.attachLibraryDbs(f.config.libraryDbs);
    REQUIRE(f.regions() == std::vector<std::string>{"ip.sv:2-6@pr8_ip"});
    f.compile(root + "/use.sv", "module pr8_use;\n  import pr8_ip::*;\n  pr8_t t;\nendmodule\n");
    CHECK(f.diags(root + "/use.sv", "reference") ==
          std::vector<std::string>{"3:3:" + note("unknown type 'pr8_t'", "ip.sv:2")});
}
