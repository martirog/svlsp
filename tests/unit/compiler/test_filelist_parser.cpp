#include <catch2/catch_test_macros.hpp>
#include "compiler/filelist_parser.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_filelist";
const std::string kSub  = kRoot + "/sub";

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

std::string writeFilelist(const std::string& name, const std::string& content) {
    std::string path = kRoot + "/" + name;
    writeFile(path, content);
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// Bare filenames
// ---------------------------------------------------------------------------

TEST_CASE("bare filenames become absolute file entries", "[compiler][filelist]") {
    std::string path = writeFilelist("bare.f",
        kRoot + "/a.sv\n" + kRoot + "/b.sv\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.files.size() == 2);
    CHECK(config.files[0] == kRoot + "/a.sv");
    CHECK(config.files[1] == kRoot + "/b.sv");
}

// ---------------------------------------------------------------------------
// Chained +define+/+incdir+/+libext+
// ---------------------------------------------------------------------------

TEST_CASE("chained define values with and without a value", "[compiler][filelist]") {
    std::string path = writeFilelist("defines.f", "+define+WIDTH=8+DEBUG\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.defines.count("WIDTH"));
    CHECK(config.defines.at("WIDTH") == "8");
    REQUIRE(config.defines.count("DEBUG"));
    CHECK(config.defines.at("DEBUG") == "");
}

TEST_CASE("chained incdir values", "[compiler][filelist]") {
    std::string path = writeFilelist("incdir.f",
        "+incdir+" + kRoot + "/inc1+" + kRoot + "/inc2\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.includeDirs.size() == 2);
    CHECK(config.includeDirs[0] == kRoot + "/inc1");
    CHECK(config.includeDirs[1] == kRoot + "/inc2");
}

TEST_CASE("chained libext values preserve order", "[compiler][filelist]") {
    std::string path = writeFilelist("libext.f", "+libext+.sv+.v\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.libExtensions.size() == 2);
    CHECK(config.libExtensions[0] == ".sv");
    CHECK(config.libExtensions[1] == ".v");
}

// ---------------------------------------------------------------------------
// Mode switches
// ---------------------------------------------------------------------------

TEST_CASE("-sv sets SystemVerilog mode", "[compiler][filelist]") {
    std::string path = writeFilelist("sv.f", "-sv\n");
    auto config = FilelistParser::parse(path);
    CHECK(config.mode == SvLanguageMode::SystemVerilog);
}

TEST_CASE("-sverilog sets SystemVerilog mode", "[compiler][filelist]") {
    std::string path = writeFilelist("sverilog.f", "-sverilog\n");
    auto config = FilelistParser::parse(path);
    CHECK(config.mode == SvLanguageMode::SystemVerilog);
}

// ---------------------------------------------------------------------------
// -y / -v / -top
// ---------------------------------------------------------------------------

TEST_CASE("-y, -v, -top are captured", "[compiler][filelist]") {
    std::string path = writeFilelist("ylibtop.f",
        "-y " + kRoot + "/libs\n"
        "-v " + kRoot + "/vendor.v\n"
        "-top top_module\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.libraryDirs.size() == 1);
    CHECK(config.libraryDirs[0] == kRoot + "/libs");
    REQUIRE(config.libraryFiles.size() == 1);
    CHECK(config.libraryFiles[0] == kRoot + "/vendor.v");
    CHECK(config.topModule == "top_module");
}

// ---------------------------------------------------------------------------
// -f (CWD-relative) vs -F (filelist-relative) nested inclusion
// ---------------------------------------------------------------------------

TEST_CASE("-f nested filelist: relative paths inside resolve against CWD",
          "[compiler][filelist]") {
    writeFile(kSub + "/nested_f.f", "rel_via_f.sv\n");
    std::string mainPath = writeFilelist("main_f.f", "-f " + kSub + "/nested_f.f\n");

    auto config = FilelistParser::parse(mainPath);

    REQUIRE(config.files.size() == 1);
    std::string expected = (fs::current_path() / "rel_via_f.sv").lexically_normal().string();
    CHECK(config.files[0] == expected);
}

TEST_CASE("-F nested filelist: relative paths inside resolve against its own directory",
          "[compiler][filelist]") {
    writeFile(kSub + "/nested_F.f", "rel_via_F.sv\n");
    std::string mainPath = writeFilelist("main_F.f", "-F " + kSub + "/nested_F.f\n");

    auto config = FilelistParser::parse(mainPath);

    REQUIRE(config.files.size() == 1);
    CHECK(config.files[0] == kSub + "/rel_via_F.sv");
}

TEST_CASE("self-referential -f cycle throws", "[compiler][filelist]") {
    std::string path = kRoot + "/cycle.f";
    writeFile(path, "-f " + path + "\n");

    REQUIRE_THROWS_AS(FilelistParser::parse(path), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

TEST_CASE("unsupported switch throws with the offending token in the message",
          "[compiler][filelist]") {
    std::string path = writeFilelist("bad_switch.f", "-xyz\n");

    try {
        FilelistParser::parse(path);
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("-xyz") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Comments and quoted filenames
// ---------------------------------------------------------------------------

TEST_CASE("line comments are stripped", "[compiler][filelist]") {
    std::string path = writeFilelist("comments.f",
        "// full line comment\n" +
        kRoot + "/a.sv // trailing comment\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.files.size() == 1);
    CHECK(config.files[0] == kRoot + "/a.sv");
}

TEST_CASE("quoted filenames with spaces are a single token", "[compiler][filelist]") {
    std::string path = writeFilelist("quoted.f",
        "\"" + kRoot + "/name with spaces.sv\"\n");

    auto config = FilelistParser::parse(path);

    REQUIRE(config.files.size() == 1);
    CHECK(config.files[0] == kRoot + "/name with spaces.sv");
}
