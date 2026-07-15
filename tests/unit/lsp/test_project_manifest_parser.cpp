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
        "libExtensions": [".sv", ".v"]
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
