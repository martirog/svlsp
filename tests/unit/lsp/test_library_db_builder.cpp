#include <catch2/catch_test_macros.hpp>
#include "lsp/library_db_builder.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const std::string kRoot = "/tmp/svlsp_test_library_db_builder";

void writeFile(const std::string& path, const std::string& content) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << content;
}

} // namespace

TEST_CASE("LibraryDbBuilder: builds a persistent DB from a .f filelist",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/filelist";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath);

    REQUIRE(result.ok);
    CHECK(result.error.empty());
    CHECK(result.fileCount == 1);
    CHECK(result.diagnosticCount == 0);
    REQUIRE(fs::exists(dbPath));

    // The DB is a real, independent file -- reopening it (a fresh
    // connection, as a later server/attach would do) must see the same
    // compiled symbols, not just an in-process cache.
    Database db(dbPath);
    SymbolDatabase sdb(db);
    auto rows = sdb.symbolsForFile(root + "/top.sv");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "top_mod");
    CHECK(rows[0].kind == "Module");
}

TEST_CASE("LibraryDbBuilder: builds a persistent DB from a .svlsp.json manifest",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/manifest";
    writeFile(root + "/a.sv", "module a_mod; endmodule\n");
    writeFile(root + "/proj.svlsp.json", R"({"files": ["a.sv"]})");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/proj.svlsp.json", dbPath);

    REQUIRE(result.ok);
    CHECK(result.fileCount == 1);
    REQUIRE(fs::exists(dbPath));

    Database db(dbPath);
    SymbolDatabase sdb(db);
    auto rows = sdb.symbolsForFile(root + "/a.sv");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "a_mod");
}

TEST_CASE("LibraryDbBuilder: counts diagnostics across the whole project",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/diagnostics";
    // top instantiates leaf_mod, which is never declared or library-resolved
    // here -- an unresolved-instantiation diagnostic on top.sv.
    writeFile(root + "/top.sv", "module top; leaf_mod u_leaf(); endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath);

    REQUIRE(result.ok);
    CHECK(result.diagnosticCount >= 1);
}

TEST_CASE("LibraryDbBuilder: a bad config path fails closed without creating the DB file",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/bad-config";
    fs::create_directories(root);
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/does_not_exist.f", dbPath);

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
    CHECK_FALSE(fs::exists(dbPath));
}
