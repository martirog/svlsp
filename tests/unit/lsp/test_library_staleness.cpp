// Stale library DBs (plan.md §6.34 S7): a library DB says whether it still
// matches its sources; an svlsp-owned cache is rebuilt from a copy of it;
// `--build-db --check` tells without writing.
#include <catch2/catch_test_macros.hpp>
#include "lsp/library_db_builder.h"
#include "db/database.h"
#include "db/library_check.h"
#include "db/symbol_database.h"
#include "compiler/project_config.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

struct Lib {
    std::string dir;
    explicit Lib(const std::string& name) : dir("/tmp/svlsp_test_stale/" + name)
    {
        fs::remove_all(dir);
        fs::create_directories(dir);
        write("lib_pkg.sv", "package st_lib;\n`include \"lib_cls.svh\"\nendpackage\n");
        write("lib_cls.svh", "class st_cls; endclass\n");
        write("lib.json", R"({"files": ["lib_pkg.sv"]})");
    }
    // Written well in the past, so its stat data is trusted.
    void write(const std::string& rel, const std::string& text, int secondsAgo = 120)
    {
        const std::string p = dir + "/" + rel;
        std::ofstream(p) << text;
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::seconds(secondsAgo));
    }
    std::string path(const std::string& rel) const { return dir + "/" + rel; }
    bool has(const std::string& db, const std::string& name) const
    {
        Database d(":memory:");
        d.reopen(db, /*readOnly=*/true);
        SymbolDatabase sdb(d);
        return !sdb.findSymbolsByName(name).empty();
    }
};

} // namespace

TEST_CASE("library staleness: an unchanged library reads no file; an edit or a deletion is stale",
          "[library][stale][s7]")
{
    Lib lib("check");
    REQUIRE(LibraryDbBuilder::build(lib.path("lib.json"), lib.path("lib.db")).ok);

    auto st = checkLibraryDb(lib.path("lib.db"));
    CHECK(st.total == 2);
    CHECK_FALSE(st.stale());
    CHECK(st.read == 0); // stat data alone

    lib.write("lib_cls.svh", "class st_cls; int x; endclass\n", 60);
    st = checkLibraryDb(lib.path("lib.db"));
    CHECK(st.stale());
    CHECK(st.changed == 1);
    CHECK(st.describe().rfind("1 of 2 files changed", 0) == 0);

    // A touch with the same text is read, and is not stale.
    lib.write("lib_cls.svh", "class st_cls; endclass\n", 30);
    CHECK_FALSE(checkLibraryDb(lib.path("lib.db")).stale());

    fs::remove(lib.path("lib_cls.svh"));
    st = checkLibraryDb(lib.path("lib.db"));
    CHECK(st.missing == 1);
    CHECK(st.stale());
}

TEST_CASE("library staleness: a stale svlsp-owned cache is rebuilt in place",
          "[library][stale][s7]")
{
    Lib lib("owned");
    ProjectConfig config;
    config.libraryDbSources.push_back({lib.path("lib.json"), lib.path("cache.db")});
    std::vector<std::string> rebuilt;
    LibraryDbBuilder::resolveLibraryDbSources(config, nullptr, "", {}, &rebuilt);
    CHECK(rebuilt.empty()); // built, not rebuilt
    REQUIRE(lib.has(lib.path("cache.db"), "st_cls"));

    lib.write("lib_cls.svh", "class st_renamed; endclass\n", 60);
    ProjectConfig again;
    again.libraryDbSources = config.libraryDbSources;
    LibraryDbBuilder::resolveLibraryDbSources(again, nullptr, "", {}, &rebuilt);
    CHECK(rebuilt == std::vector<std::string>{lib.path("cache.db")});
    CHECK(lib.has(lib.path("cache.db"), "st_renamed"));
    CHECK_FALSE(lib.has(lib.path("cache.db"), "st_cls"));
    CHECK_FALSE(fs::exists(lib.path("cache.db.tmp")));
    CHECK_FALSE(checkLibraryDb(lib.path("cache.db")).stale());
}

TEST_CASE("--build-db --check: up to date, then stale after an edit, without writing",
          "[library][stale][s7]")
{
    Lib lib("cli");
    REQUIRE(LibraryDbBuilder::build(lib.path("lib.json"), lib.path("lib.db"), nullptr, "v1").ok);
    auto r = LibraryDbBuilder::check(lib.path("lib.json"), lib.path("lib.db"), "v1");
    REQUIRE(r.ok);
    CHECK_FALSE(r.stale);

    CHECK(LibraryDbBuilder::check(lib.path("lib.json"), lib.path("lib.db"), "v2").stale);

    lib.write("lib_cls.svh", "class st_cls; int y; endclass\n", 60);
    r = LibraryDbBuilder::check(lib.path("lib.json"), lib.path("lib.db"), "v1");
    CHECK(r.stale);
    CHECK(r.why.find("units to compile") != std::string::npos);
    // Nothing was written: still stale, and the old rows are there.
    CHECK(LibraryDbBuilder::check(lib.path("lib.json"), lib.path("lib.db"), "v1").stale);

    CHECK(LibraryDbBuilder::check(lib.path("lib.json"), lib.path("missing.db"), "v1").stale);
}

// ---------------------------------------------------------------------------
// plan.md §6.40 D1: what a library DB holds, and what makes it unusable.
// ---------------------------------------------------------------------------

TEST_CASE("inspectLibraryDb: counts a built library; a missing, foreign or empty file is a "
          "problem", "[library][d1]")
{
    Lib lib("inspect");
    REQUIRE(LibraryDbBuilder::build(lib.path("lib.json"), lib.path("lib.db")).ok);

    const auto good = inspectLibraryDb(lib.path("lib.db"));
    CHECK(good.exists);
    CHECK(good.error.empty());
    CHECK(good.files == 2);
    CHECK(good.symbols >= 2); // st_lib, st_cls
    CHECK(libraryDbProblems(good).empty());
    CHECK(libraryDbSummary(good) == "2 files, " + std::to_string(good.symbols) + " symbols");

    const auto missing = inspectLibraryDb(lib.path("nope.db"));
    CHECK_FALSE(missing.exists);
    REQUIRE(libraryDbProblems(missing).size() == 1);
    CHECK(libraryDbProblems(missing)[0] == "does not exist");
    CHECK_FALSE(fs::exists(lib.path("nope.db"))); // looking doesn't create it

    lib.write("junk.db", "not a database\n");
    const auto junk = inspectLibraryDb(lib.path("junk.db"));
    CHECK(junk.exists);
    CHECK_FALSE(junk.error.empty());
    REQUIRE(libraryDbProblems(junk).size() == 1);
    CHECK(libraryDbProblems(junk)[0].rfind("can't be read as an svlsp database", 0) == 0);

    {
        Database d(lib.path("empty.db"));
        d.initSchema();
    }
    const auto empty = inspectLibraryDb(lib.path("empty.db"));
    CHECK(empty.error.empty());
    CHECK(empty.files == 0);
    REQUIRE(libraryDbProblems(empty).size() == 1);
    CHECK(libraryDbProblems(empty)[0] == "holds no files (an empty database: a wrong path, or a "
                                         "failed --build-db?)");
}

TEST_CASE("inspectLibraryDb: include dirs recorded at build time that don't exist here",
          "[library][d1]")
{
    Lib lib("incdirs");
    REQUIRE(LibraryDbBuilder::build(lib.path("lib.json"), lib.path("lib.db")).ok);
    {
        Database d(lib.path("lib.db"));
        SymbolDatabase sdb(d);
        sdb.setLibraryIncludeDirs({lib.dir, "/no/such/uvm/src"});
    }
    const auto info = inspectLibraryDb(lib.path("lib.db"));
    REQUIRE(info.missingIncludeDirs.size() == 1);
    CHECK(info.missingIncludeDirs[0] == "/no/such/uvm/src");
    REQUIRE(libraryDbProblems(info).size() == 1);
    CHECK(libraryDbProblems(info)[0] ==
          "records include dirs that don't exist here: /no/such/uvm/src (built on another "
          "machine or from another path?)");
}

TEST_CASE("--build-db reports a library DB its config names that is missing (plan.md §6.40 D1)",
          "[library][d1]")
{
    Lib lib("builddb");
    // A missing file, one in a directory that doesn't exist, and a file
    // that isn't a database: each reported, none attached, the build fine.
    lib.write("top.json", R"({"files": ["lib_pkg.sv"], "libraryDbs": ["missing_uvm.db",)"
                          R"( "no_dir/uvm.db", "lib_cls.svh"]})");
    const auto r = LibraryDbBuilder::build(lib.path("top.json"), lib.path("top.db"));
    REQUIRE(r.ok);
    REQUIRE(r.libraryWarnings.size() == 3);
    CHECK(r.libraryWarnings[0] == "library DB " + lib.path("missing_uvm.db") + " (listed in " +
                                      lib.path("top.json") + ") does not exist");
    CHECK(r.libraryWarnings[1].find("no_dir/uvm.db") != std::string::npos);
    CHECK(r.libraryWarnings[2].rfind("library DB " + lib.path("lib_cls.svh") + " (listed in " +
                                         lib.path("top.json") +
                                         ") can't be read as an svlsp database", 0) == 0);
    CHECK_FALSE(fs::exists(lib.path("missing_uvm.db")));
}
