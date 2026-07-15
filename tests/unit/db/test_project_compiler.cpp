#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "db/project_compiler.h"
#include "compiler/project_config.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_project_compiler";

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

struct Fixture {
    Database              db;
    SymbolDatabase        sdb;
    CompilationController ctrl;

    Fixture() : db(":memory:"), sdb(db), ctrl(sdb) { db.initSchema(); }
};

} // namespace

TEST_CASE("loadProject compiles every explicit file", "[db][project-compiler]") {
    std::string aPath = kRoot + "/a.sv";
    std::string bPath = kRoot + "/b.sv";
    writeFile(aPath, "module a_mod; endmodule\n");
    writeFile(bPath, "module b_mod; endmodule\n");

    Fixture f;
    ProjectConfig config;
    config.files = {aPath, bPath};

    int compiled = ProjectCompiler::loadProject(config, f.ctrl, f.sdb);

    CHECK(compiled == 2);
    REQUIRE(f.sdb.symbolsForFile(aPath).size() == 1);
    CHECK(f.sdb.symbolsForFile(aPath)[0].name == "a_mod");
    REQUIRE(f.sdb.symbolsForFile(bPath).size() == 1);
    CHECK(f.sdb.symbolsForFile(bPath)[0].name == "b_mod");
}

TEST_CASE("loadProject skips a missing explicit file without throwing",
          "[db][project-compiler]") {
    std::string aPath = kRoot + "/present.sv";
    writeFile(aPath, "module present_mod; endmodule\n");

    Fixture f;
    ProjectConfig config;
    config.files = {aPath, kRoot + "/does_not_exist.sv"};

    int compiled = ProjectCompiler::loadProject(config, f.ctrl, f.sdb);

    CHECK(compiled == 1);
    REQUIRE(f.sdb.symbolsForFile(aPath).size() == 1);
}

TEST_CASE("loadProject's config defines gate an ifdef in an explicit file",
          "[db][project-compiler]") {
    std::string aPath = kRoot + "/ifdef_gated.sv";
    writeFile(aPath, "`ifdef SIM\nmodule sim_only; endmodule\n`endif\n");

    Fixture f;
    ProjectConfig config;
    config.files = {aPath};
    config.defines["SIM"] = "";

    ProjectCompiler::loadProject(config, f.ctrl, f.sdb);

    auto syms = f.sdb.symbolsForFile(aPath);
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "sim_only");
}

TEST_CASE("loadProject's config includeDirs resolve a bare `include in an explicit file",
          "[db][project-compiler]") {
    std::string incPath = kRoot + "/inc/via_includedir.sv";
    writeFile(incPath, "module via_includedir; endmodule\n");

    std::string mainPath = kRoot + "/main_with_incdir.sv";
    writeFile(mainPath, "`include \"via_includedir.sv\"\n");

    Fixture f;
    ProjectConfig config;
    config.files       = {mainPath};
    config.includeDirs = {kRoot + "/inc"};

    ProjectCompiler::loadProject(config, f.ctrl, f.sdb);

    auto syms = f.sdb.symbolsForFile(incPath);
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "via_includedir");
}

TEST_CASE("loadProject invokes LibraryResolver for unresolved instantiations",
          "[db][project-compiler]") {
    std::string topPath = kRoot + "/top_needs_lib.sv";
    writeFile(topPath, "module top; leaf_mod u0(); endmodule\n");

    std::string libPath = kRoot + "/lib/leaf_mod.sv";
    writeFile(libPath, "module leaf_mod; endmodule\n");

    Fixture f;
    ProjectConfig config;
    config.files         = {topPath};
    config.libraryDirs   = {kRoot + "/lib"};
    config.libExtensions = {".sv"};

    int compiled = ProjectCompiler::loadProject(config, f.ctrl, f.sdb);

    CHECK(compiled == 2); // top_needs_lib.sv + library-resolved leaf_mod.sv
    CHECK(f.sdb.unresolvedInstantiatedTypeNames().empty());
    REQUIRE(!f.sdb.symbolsForFile(libPath).empty());
}
