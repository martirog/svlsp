#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "db/library_resolver.h"
#include "compiler/project_config.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_library_resolver";

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

struct Fixture {
    Database              db;
    SymbolDatabase        sdb;
    CompilationController ctrl;

    Fixture() : db(":memory:"), sdb(db), ctrl(sdb) { db.initSchema(); }
};

} // namespace

TEST_CASE("-v resolution: referenced module is compiled, unreferenced one isn't",
          "[db][library-resolver]") {
    std::string usedPath   = kRoot + "/used_mod.v";
    std::string unusedPath = kRoot + "/unused_mod.v";
    writeFile(usedPath,   "module used_mod; endmodule\n");
    writeFile(unusedPath, "module unused_mod; endmodule\n");

    Fixture f;
    f.ctrl.compile("/top.sv", "module top; used_mod u0(); endmodule\n");

    ProjectConfig config;
    config.libraryFiles = {usedPath, unusedPath};

    int compiled = LibraryResolver::resolve(config, f.ctrl, f.sdb);

    CHECK(compiled == 1);
    CHECK(!f.sdb.getFileHash(usedPath).empty());
    CHECK(f.sdb.getFileHash(unusedPath).empty());
}

TEST_CASE("-y resolution prefers the first matching library directory",
          "[db][library-resolver]") {
    std::string dirA = kRoot + "/dirA";
    std::string dirB = kRoot + "/dirB";
    // Both declare shared_mod; only dirB's version additionally instantiates
    // "nested_from_b". If dirA (declared first) wins, nested_from_b never
    // shows up as an unresolved name.
    writeFile(dirA + "/shared_mod.sv", "module shared_mod; endmodule\n");
    writeFile(dirB + "/shared_mod.sv", "module shared_mod; nested_from_b u1(); endmodule\n");

    Fixture f;
    f.ctrl.compile("/top.sv", "module top; shared_mod u0(); endmodule\n");

    ProjectConfig config;
    config.libraryDirs   = {dirA, dirB};
    config.libExtensions = {".sv"};

    LibraryResolver::resolve(config, f.ctrl, f.sdb);

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(!contains(unresolved, "nested_from_b"));
}

TEST_CASE("-y resolution prefers the first matching +libext+ extension",
          "[db][library-resolver]") {
    std::string dir = kRoot + "/extdir";
    // Both declare ext_mod; only the .sv version instantiates "nested_from_sv".
    writeFile(dir + "/ext_mod.v",  "module ext_mod; endmodule\n");
    writeFile(dir + "/ext_mod.sv", "module ext_mod; nested_from_sv u1(); endmodule\n");

    Fixture f;
    f.ctrl.compile("/top.sv", "module top; ext_mod u0(); endmodule\n");

    ProjectConfig config;
    config.libraryDirs   = {dir};
    config.libExtensions = {".v", ".sv"}; // .v declared first -> must win

    LibraryResolver::resolve(config, f.ctrl, f.sdb);

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(!contains(unresolved, "nested_from_sv"));
}

TEST_CASE("recursive fixpoint resolution follows an A->B->C chain",
          "[db][library-resolver]") {
    std::string dir = kRoot + "/chain";
    writeFile(dir + "/chain_b.sv", "module chain_b; chain_c u1(); endmodule\n");
    writeFile(dir + "/chain_c.sv", "module chain_c; endmodule\n");

    Fixture f;
    f.ctrl.compile("/top.sv", "module top; chain_b u0(); endmodule\n");

    ProjectConfig config;
    config.libraryDirs   = {dir};
    config.libExtensions = {".sv"};

    int compiled = LibraryResolver::resolve(config, f.ctrl, f.sdb);

    CHECK(compiled == 2);
    CHECK(f.sdb.unresolvedInstantiatedTypeNames().empty());
    REQUIRE(!f.sdb.symbolsForFile(dir + "/chain_b.sv").empty());
    REQUIRE(!f.sdb.symbolsForFile(dir + "/chain_c.sv").empty());
}

TEST_CASE("a name with no -v/-y match stays unresolved and compiles nothing",
          "[db][library-resolver]") {
    Fixture f;
    f.ctrl.compile("/top.sv", "module top; ghost_mod u0(); endmodule\n");

    ProjectConfig config; // no libraryFiles/libraryDirs at all

    int compiled = LibraryResolver::resolve(config, f.ctrl, f.sdb);

    CHECK(compiled == 0);
    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(contains(unresolved, "ghost_mod"));
}

TEST_CASE("an unresolved instantiation gets a diagnostic on the referencing file",
          "[db][library-resolver]") {
    Fixture f;
    f.ctrl.compile("/top.sv", "module top; ghost_mod u0(); endmodule\n");

    ProjectConfig config;
    LibraryResolver::resolve(config, f.ctrl, f.sdb);

    auto diags = f.sdb.diagnosticsForFile("/top.sv");
    bool found = false;
    for (const auto& d : diags) {
        if (d.message.find("ghost_mod") != std::string::npos) { found = true; break; }
    }
    CHECK(found);
}
