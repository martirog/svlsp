#include <catch2/catch_test_macros.hpp>
#include "lsp/library_db_builder.h"
#include "compiler/project_config.h"
#include "compiler/parse_record.h"
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

TEST_CASE("LibraryDbBuilder: bakes the config's resolved includeDirs into the output DB "
          "(plan.md §6.19 piece 4)",
          "[lsp][library-db-builder][library-include-dirs]")
{
    std::string root = kRoot + "/bakes-include-dirs";
    writeFile(root + "/headers/macros.svh", "module from_header; endmodule\n");
    writeFile(root + "/top.sv", "`include \"macros.svh\"\n");
    writeFile(root + "/proj.f", "top.sv\n+incdir+headers\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath);
    REQUIRE(result.ok);

    // Reopening the DB fresh (a later project attaching it would do the
    // same) must see the includeDirs this build resolved from proj.f's own
    // +incdir+, without needing proj.f itself to still exist alongside it.
    Database db(dbPath);
    SymbolDatabase sdb(db);
    CHECK(sdb.libraryIncludeDirs() ==
          std::vector<std::string>{fs::path(root + "/headers").lexically_normal().string()});
}

TEST_CASE("LibraryDbBuilder: creates a not-yet-existing output directory",
          "[lsp][library-db-builder]")
{
    // Regression test: sqlite3_open (inside the Database constructor) does
    // not create missing parent directories on its own. Found live against
    // a real server session where --output/cachePath's own directory
    // ("/var/cache/svlsp/"-style, per docs/usage.md's own libraryDbSources
    // example) didn't exist yet -- build() threw, and since it was reached
    // through a plain textDocument/didOpen notification handler
    // (ProjectRegistry -> resolveLibraryDbSources -> build()), the
    // exception was silently dropped by lsp-framework's own dispatch
    // (notifications get no error response to report it on) with no trace
    // anywhere. Every other test in this file writes its config/source
    // files into the same directory as the output DB first, which
    // incidentally already creates that directory -- this is the one test
    // that deliberately keeps the output directory brand new.
    std::string root = kRoot + "/new-output-dir";
    writeFile(root + "/config/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/config/proj.f", "top.sv\n");
    std::string dbPath = root + "/not-yet-created/out.db";
    fs::remove_all(root + "/not-yet-created");
    REQUIRE_FALSE(fs::exists(root + "/not-yet-created"));

    auto result = LibraryDbBuilder::build(root + "/config/proj.f", dbPath);

    REQUIRE(result.ok);
    CHECK(result.error.empty());
    REQUIRE(fs::exists(dbPath));
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

TEST_CASE("LibraryDbBuilder: an unopenable output path fails closed rather than throwing",
          "[lsp][library-db-builder]")
{
    // build()'s contract is to never throw -- verified here by making the
    // *output* path itself unopenable (a directory sitting where the DB
    // file needs to go), not just an unparseable config, since the fix for
    // the not-yet-existing-directory bug above widened the try/catch to
    // cover the whole function, not just config parsing.
    std::string root = kRoot + "/unopenable-output";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove_all(dbPath);
    fs::create_directories(dbPath); // a directory, not a file, at dbPath

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath);

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

// ---------------------------------------------------------------------------
// resolveLibraryDbSources (plan.md §6.19 piece 3)
// ---------------------------------------------------------------------------

TEST_CASE("resolveLibraryDbSources builds a missing cache and appends it to libraryDbs",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/resolve-missing";
    writeFile(root + "/uvm_like.sv", "class uvm_object; endclass\n");
    writeFile(root + "/uvm.f", "uvm_like.sv\n");
    std::string cachePath = root + "/uvm-cache.db";
    fs::remove(cachePath);

    ProjectConfig config;
    config.libraryDbSources = {{root + "/uvm.f", cachePath}};

    LibraryDbBuilder::resolveLibraryDbSources(config);

    REQUIRE(fs::exists(cachePath));
    REQUIRE(config.libraryDbs.size() == 1);
    CHECK(config.libraryDbs[0] == cachePath);

    Database db(cachePath);
    SymbolDatabase sdb(db);
    CHECK(sdb.findSymbolsByName("uvm_object").size() == 1);
}

TEST_CASE("resolveLibraryDbSources reuses an already-existing cache without rebuilding it",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/resolve-existing";
    // The config, if actually (re)built, would produce "new_class" -- the
    // pre-populated cache instead has "old_class", proving a hit skips the
    // rebuild entirely rather than silently overwriting the cache.
    writeFile(root + "/lib.sv", "class new_class; endclass\n");
    writeFile(root + "/lib.f", "lib.sv\n");
    std::string cachePath = root + "/cache.db";
    fs::remove(cachePath);
    {
        Database db(cachePath);
        db.initSchema();
        SymbolDatabase sdb(db);
        sdb.replaceSymbols(sdb.upsertFile(root + "/prebuilt.sv", "h"),
            {{ParseRecordKind::Class, "old_class", 1, 6, "", "", 1, ""}});
    }

    ProjectConfig config;
    config.libraryDbSources = {{root + "/lib.f", cachePath}};

    LibraryDbBuilder::resolveLibraryDbSources(config);

    REQUIRE(config.libraryDbs.size() == 1);
    Database db(cachePath);
    SymbolDatabase sdb(db);
    CHECK(sdb.findSymbolsByName("old_class").size() == 1);  // untouched
    CHECK(sdb.findSymbolsByName("new_class").size() == 0);  // never built
}

TEST_CASE("resolveLibraryDbSources throws if building a missing cache fails",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/resolve-failure";
    fs::create_directories(root);
    std::string cachePath = root + "/cache.db";
    fs::remove(cachePath);

    ProjectConfig config;
    config.libraryDbSources = {{root + "/does_not_exist.f", cachePath}};

    CHECK_THROWS_AS(LibraryDbBuilder::resolveLibraryDbSources(config), std::runtime_error);
    CHECK_FALSE(fs::exists(cachePath));
}

TEST_CASE("build() transitively resolves its own config's libraryDbSources",
          "[lsp][library-db-builder]")
{
    std::string root = kRoot + "/transitive";
    // "top" library depends on "nested", lazily cached, referenced only via
    // top.svlsp.json's own libraryDbSources -- proving build() resolves its
    // own config's sources before compiling, not just direct build() calls.
    writeFile(root + "/nested.sv", "class nested_class; endclass\n");
    writeFile(root + "/nested.f", "nested.sv\n");
    writeFile(root + "/top.sv", "class top_class; endclass\n");
    std::string nestedCache = root + "/nested-cache.db";
    fs::remove(nestedCache);
    writeFile(root + "/top.svlsp.json",
        "{\"files\": [\"top.sv\"], \"libraryDbSources\": "
        "[{\"config\": \"" + root + "/nested.f\", \"cache\": \"" + nestedCache + "\"}]}");
    std::string outputPath = root + "/top.db";
    fs::remove(outputPath);

    auto result = LibraryDbBuilder::build(root + "/top.svlsp.json", outputPath);

    REQUIRE(result.ok);
    REQUIRE(fs::exists(nestedCache)); // built as a side effect

    // top.db's own symbols: just top_class. ATTACH is a live, per-connection
    // relationship (the whole point of attach-and-query over a physical
    // merge, plan.md §6.19) -- it's never persisted into the output file
    // itself, so a *freshly reopened* top.db was never attached to anything
    // and correctly has no memory of nested_class. What this test actually
    // needs to prove is that the recursive build() call happened at all and
    // produced a real, independently-correct nested-cache.db -- checked via
    // its own fresh connection below, exactly as a later project attaching
    // it would see.
    {
        Database db(outputPath);
        SymbolDatabase sdb(db);
        CHECK(sdb.findSymbolsByName("top_class").size() == 1);
        CHECK(sdb.findSymbolsByName("nested_class").size() == 0);
    }
    {
        Database nestedDb(nestedCache);
        SymbolDatabase nestedSdb(nestedDb);
        CHECK(nestedSdb.findSymbolsByName("nested_class").size() == 1);
    }
}

// ---------------------------------------------------------------------------
// currentVersion stale-cache guard: a real user bug report -- re-running
// `svlsp --build-db` against an already-populated DB with a fixed/upgraded
// binary (no source file content changed) silently kept every stale,
// pre-fix parse result, since the per-file content-hash cache alone has no
// way to know the *parser* itself changed. See LibraryDbBuilder::build's own
// doc comment and SymbolDatabase::resetAllFiles/builtByVersion.
// ---------------------------------------------------------------------------

TEST_CASE("LibraryDbBuilder: records currentVersion into the output DB",
          "[lsp][library-db-builder][built-by-version]")
{
    std::string root = kRoot + "/records-version";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath, nullptr, "v1");

    REQUIRE(result.ok);
    Database db(dbPath);
    SymbolDatabase sdb(db);
    CHECK(sdb.builtByVersion() == "v1");
}

TEST_CASE("LibraryDbBuilder: an empty currentVersion never records or checks a version "
          "(every pre-existing call site's behavior, unchanged)",
          "[lsp][library-db-builder][built-by-version]")
{
    std::string root = kRoot + "/no-version-info";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath);

    REQUIRE(result.ok);
    Database db(dbPath);
    SymbolDatabase sdb(db);
    CHECK(sdb.builtByVersion() == "");
}

TEST_CASE("LibraryDbBuilder: re-building with the same currentVersion reuses the "
          "per-file content-hash cache",
          "[lsp][library-db-builder][built-by-version]")
{
    std::string root = kRoot + "/same-version-reuses-cache";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    REQUIRE(LibraryDbBuilder::build(root + "/proj.f", dbPath, nullptr, "v1").ok);

    // Plant a symbol name a real recompile of this exact source would never
    // produce (upsertFile with the file's own current hash reuses its
    // existing row rather than creating a new one), proving on its own
    // survival whether the second build() call below actually skipped
    // reparsing.
    {
        Database db(dbPath);
        SymbolDatabase sdb(db);
        auto fileId = sdb.upsertFile(root + "/top.sv", sdb.getFileHash(root + "/top.sv"));
        sdb.replaceSymbols(fileId, {{ParseRecordKind::Module, "planted_stale_name", 1, 0, "", "",
                                     1}});
    }

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath, nullptr, "v1");
    REQUIRE(result.ok);

    Database db(dbPath);
    SymbolDatabase sdb(db);
    CHECK(sdb.findSymbolsByName("planted_stale_name").size() == 1);
    CHECK(sdb.findSymbolsByName("top_mod").size() == 0);
}

TEST_CASE("LibraryDbBuilder: re-building with a different currentVersion discards the "
          "stale per-file content-hash cache",
          "[lsp][library-db-builder][built-by-version]")
{
    std::string root = kRoot + "/different-version-forces-rebuild";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    REQUIRE(LibraryDbBuilder::build(root + "/proj.f", dbPath, nullptr, "v1").ok);

    // Same plant as the "same version" test above -- proves whether the
    // second build() call actually reparsed (discarding this) or not.
    {
        Database db(dbPath);
        SymbolDatabase sdb(db);
        auto fileId = sdb.upsertFile(root + "/top.sv", sdb.getFileHash(root + "/top.sv"));
        sdb.replaceSymbols(fileId, {{ParseRecordKind::Module, "planted_stale_name", 1, 0, "", "",
                                     1}});
    }

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath, nullptr, "v2");
    REQUIRE(result.ok);

    Database db(dbPath);
    SymbolDatabase sdb(db);
    CHECK(sdb.findSymbolsByName("planted_stale_name").size() == 0);
    CHECK(sdb.findSymbolsByName("top_mod").size() == 1);
    CHECK(sdb.builtByVersion() == "v2");
}

TEST_CASE("LibraryDbBuilder: a DB predating this feature (no recorded version) is "
          "treated as stale by any real currentVersion",
          "[lsp][library-db-builder][built-by-version]")
{
    std::string root = kRoot + "/predates-feature";
    writeFile(root + "/top.sv", "module top_mod; endmodule\n");
    writeFile(root + "/proj.f", "top.sv\n");
    std::string dbPath = root + "/out.db";
    fs::remove(dbPath);

    // Build with no version info at all (library_build_info stays empty),
    // simulating a DB produced before this feature existed.
    REQUIRE(LibraryDbBuilder::build(root + "/proj.f", dbPath).ok);
    {
        Database db(dbPath);
        SymbolDatabase sdb(db);
        auto fileId = sdb.upsertFile(root + "/top.sv", sdb.getFileHash(root + "/top.sv"));
        sdb.replaceSymbols(fileId, {{ParseRecordKind::Module, "planted_stale_name", 1, 0, "", "",
                                     1}});
    }

    auto result = LibraryDbBuilder::build(root + "/proj.f", dbPath, nullptr, "v1");
    REQUIRE(result.ok);

    Database db(dbPath);
    SymbolDatabase sdb(db);
    CHECK(sdb.findSymbolsByName("planted_stale_name").size() == 0);
    CHECK(sdb.findSymbolsByName("top_mod").size() == 1);
    CHECK(sdb.builtByVersion() == "v1");
}
