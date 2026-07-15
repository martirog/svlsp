#include <catch2/catch_test_macros.hpp>
#include "lsp/project_registry.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_project_registry";

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

struct Fixture {
    Database              db;
    SymbolDatabase        sdb;
    CompilationController ctrl;
    ProjectRegistry        registry;

    Fixture() : db(":memory:"), sdb(db), ctrl(sdb), registry(ctrl, sdb) { db.initSchema(); }
};

} // namespace

TEST_CASE("configFor returns nullptr when no manifest/filelist is found",
          "[lsp][project-registry]") {
    std::string dir = kRoot + "/isolated_no_manifest/leaf";
    fs::create_directories(dir);

    Fixture f;
    CHECK(f.registry.configFor(dir + "/file.sv") == nullptr);
}

TEST_CASE("upward search finds a manifest in a parent directory",
          "[lsp][project-registry]") {
    std::string projDir = kRoot + "/upward/proj";
    writeFile(projDir + "/.svlsp.json", R"({"top": "found_upward"})");
    fs::create_directories(projDir + "/sub");

    Fixture f;
    const ProjectConfig* config = f.registry.configFor(projDir + "/sub/file.sv");

    REQUIRE(config != nullptr);
    CHECK(config->topModule == "found_upward");
}

TEST_CASE("closest directory's manifest wins over an outer one",
          "[lsp][project-registry]") {
    std::string outerDir = kRoot + "/nested/outer";
    std::string innerDir = outerDir + "/inner";
    writeFile(outerDir + "/.svlsp.json", R"({"top": "outer_top"})");
    writeFile(innerDir + "/.svlsp.json", R"({"top": "inner_top"})");

    Fixture f;
    const ProjectConfig* config = f.registry.configFor(innerDir + "/file.sv");

    REQUIRE(config != nullptr);
    CHECK(config->topModule == "inner_top");
}

TEST_CASE("precedence: .svlsp.json is preferred over svlsp.f in the same directory",
          "[lsp][project-registry]") {
    std::string dir = kRoot + "/precedence";
    writeFile(dir + "/.svlsp.json", R"({"top": "from_json"})");
    writeFile(dir + "/svlsp.f", "-top from_f\n");

    Fixture f;
    const ProjectConfig* config = f.registry.configFor(dir + "/file.sv");

    REQUIRE(config != nullptr);
    CHECK(config->topModule == "from_json");
}

TEST_CASE("a discovered project is loaded and cached only once",
          "[lsp][project-registry]") {
    std::string dir = kRoot + "/caching";
    writeFile(dir + "/.svlsp.json", R"({"top": "cached_top"})");
    fs::create_directories(dir + "/sub_a");
    fs::create_directories(dir + "/sub_b");

    Fixture f;
    const ProjectConfig* first  = f.registry.configFor(dir + "/sub_a/a.sv");
    const ProjectConfig* second = f.registry.configFor(dir + "/sub_b/b.sv");

    REQUIRE(first != nullptr);
    CHECK(first == second); // same cached instance, not re-parsed per file
}

TEST_CASE("an explicit config path overrides upward search for every file",
          "[lsp][project-registry]") {
    std::string ownDir = kRoot + "/explicit/own";
    writeFile(ownDir + "/.svlsp.json", R"({"top": "own_manifest"})");

    std::string explicitPath = kRoot + "/explicit/override.svlsp.json";
    writeFile(explicitPath, R"({"top": "explicit_override"})");

    Fixture f;
    f.registry.setExplicitConfigPath(explicitPath);
    const ProjectConfig* config = f.registry.configFor(ownDir + "/file.sv");

    REQUIRE(config != nullptr);
    CHECK(config->topModule == "explicit_override");
}
