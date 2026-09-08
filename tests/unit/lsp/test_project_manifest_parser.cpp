#include <catch2/catch_test_macros.hpp>
#include "lsp/project_manifest_parser.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_manifest";

std::string writeManifest(const std::string& name, const std::string& content) {
    fs::create_directories(kRoot);
    std::string path = kRoot + "/" + name;
    std::ofstream f(path);
    f << content;
    return path;
}

} // namespace

TEST_CASE("full manifest populates every field", "[lsp][project-manifest]") {
    std::string path = writeManifest("full.svlsp.json", R"({
        "files": ["a.sv", "b.sv"],
        "defines": {"WIDTH": "8", "SIM": ""},
        "includeDirs": ["rtl/include"],
        "top": "top_module",
        "mode": "sv",
        "libraryDirs": ["rtl/lib"],
        "libraryFiles": ["vendor/ip.v"],
        "libExtensions": [".sv", ".v"],
        "libraryDbs": ["/shared/uvm-1.2.db", "relative.db"],
        "libraryDbSources": [
            {"config": "/vip/uvm.f", "cache": "/cache/uvm-1.2.db"},
            {"config": "other.f", "cache": "relative-cache.db"}
        ]
    })");

    auto config = ProjectManifestParser::parse(path);

    REQUIRE(config.files.size() == 2);
    CHECK(config.files[0] == kRoot + "/a.sv");
    CHECK(config.files[1] == kRoot + "/b.sv");
    REQUIRE(config.defines.count("WIDTH"));
    CHECK(config.defines.at("WIDTH") == "8");
    REQUIRE(config.defines.count("SIM"));
    CHECK(config.defines.at("SIM") == "");
    REQUIRE(config.includeDirs.size() == 1);
    CHECK(config.includeDirs[0] == kRoot + "/rtl/include");
    CHECK(config.topModule == "top_module");
    CHECK(config.mode == SvLanguageMode::SystemVerilog);
    REQUIRE(config.libraryDirs.size() == 1);
    CHECK(config.libraryDirs[0] == kRoot + "/rtl/lib");
    REQUIRE(config.libraryFiles.size() == 1);
    CHECK(config.libraryFiles[0] == kRoot + "/vendor/ip.v");
    REQUIRE(config.libExtensions.size() == 2);
    CHECK(config.libExtensions[0] == ".sv");
    CHECK(config.libExtensions[1] == ".v");
    REQUIRE(config.libraryDbs.size() == 2);
    CHECK(config.libraryDbs[0] == "/shared/uvm-1.2.db"); // already absolute, unchanged
    CHECK(config.libraryDbs[1] == kRoot + "/relative.db"); // resolved against manifest dir
    REQUIRE(config.libraryDbSources.size() == 2);
    CHECK(config.libraryDbSources[0].configPath == "/vip/uvm.f");
    CHECK(config.libraryDbSources[0].cachePath == "/cache/uvm-1.2.db");
    CHECK(config.libraryDbSources[1].configPath == kRoot + "/other.f");
    CHECK(config.libraryDbSources[1].cachePath == kRoot + "/relative-cache.db");
}

TEST_CASE("partial manifest leaves unspecified fields at defaults", "[lsp][project-manifest]") {
    std::string path = writeManifest("partial.svlsp.json", R"({"files": ["only.sv"]})");

    auto config = ProjectManifestParser::parse(path);

    REQUIRE(config.files.size() == 1);
    CHECK(config.files[0] == kRoot + "/only.sv");
    CHECK(config.defines.empty());
    CHECK(config.includeDirs.empty());
    CHECK(config.topModule.empty());
    CHECK(config.mode == SvLanguageMode::SystemVerilog);
    CHECK(config.libraryDirs.empty());
    CHECK(config.libraryFiles.empty());
    CHECK(config.libExtensions.empty());
    CHECK(config.libraryDbs.empty());
    CHECK(config.libraryDbSources.empty());
}

TEST_CASE("libraryDbSources entry missing \"cache\" throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("libdbsrc_missing_cache.svlsp.json",
        R"({"libraryDbSources": [{"config": "a.f"}]})");
    CHECK_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("libraryDbSources entry that isn't an object throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("libdbsrc_not_object.svlsp.json",
        R"({"libraryDbSources": ["not-an-object"]})");
    CHECK_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("libraryDbSources that isn't an array throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("libdbsrc_not_array.svlsp.json",
        R"({"libraryDbSources": "nope"})");
    CHECK_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("mode v95 round-trips", "[lsp][project-manifest]") {
    std::string path = writeManifest("v95.svlsp.json", R"({"mode": "v95"})");

    auto config = ProjectManifestParser::parse(path);

    CHECK(config.mode == SvLanguageMode::Verilog95);
}

TEST_CASE("unknown top-level key is silently ignored", "[lsp][project-manifest]") {
    std::string path = writeManifest("unknown_key.svlsp.json",
        R"({"files": ["a.sv"], "totallyUnknownField": 42})");

    auto config = ProjectManifestParser::parse(path);

    REQUIRE(config.files.size() == 1);
    CHECK(config.files[0] == kRoot + "/a.sv");
}

TEST_CASE("malformed JSON throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("malformed.svlsp.json", "{ not valid json ");

    REQUIRE_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("non-object root throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("array_root.svlsp.json", R"(["a.sv", "b.sv"])");

    REQUIRE_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("wrongly-typed known field throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("bad_field.svlsp.json", R"({"files": "a.sv"})");

    REQUIRE_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("wrongly-typed defines value throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("bad_define.svlsp.json", R"({"defines": {"WIDTH": 8}})");

    REQUIRE_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("invalid mode value throws", "[lsp][project-manifest]") {
    std::string path = writeManifest("bad_mode.svlsp.json", R"({"mode": "vhdl"})");

    REQUIRE_THROWS_AS(ProjectManifestParser::parse(path), std::runtime_error);
}

TEST_CASE("missing manifest file throws", "[lsp][project-manifest]") {
    REQUIRE_THROWS_AS(ProjectManifestParser::parse(kRoot + "/does_not_exist.svlsp.json"),
                       std::runtime_error);
}

TEST_CASE("a bare config filename (no directory component) resolves relative paths against "
          "the real CWD, not a literal '.'",
          "[lsp][project-manifest]") {
    // Regression test: a real user bug report -- building a library DB via
    // `svlsp --build-db .svlsp.json --output uvm.db` from inside the
    // manifest's own directory (a natural workflow) baked a literal "."
    // into the persisted library_include_dirs table, instead of an
    // absolute path. That's harmless for a single live compile (the
    // process's CWD never changes mid-run), but silently wrong once a
    // *different* process reads it back later with a different CWD --
    // exactly what happens when another project attaches the prebuilt
    // library DB. Fixed by resolving the empty-baseDir fallback (a bare
    // filename has no parent_path()) against the real current_path(),
    // matching FilelistParser::parse's own already-correct convention.
    writeManifest("bare.svlsp.json", R"({"includeDirs": ["macros"]})");

    fs::path originalCwd = fs::current_path();
    fs::current_path(kRoot);
    ProjectConfig config;
    try {
        config = ProjectManifestParser::parse("bare.svlsp.json");
    } catch (...) {
        fs::current_path(originalCwd);
        throw;
    }
    fs::current_path(originalCwd);

    REQUIRE(config.includeDirs.size() == 1);
    CHECK(config.includeDirs[0] == kRoot + "/macros");
}
