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
