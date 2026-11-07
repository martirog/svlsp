// The project DB's writer lock and file modes (plan.md §6.34 S3).
#include <catch2/catch_test_macros.hpp>
#include "db/db_lock.h"
#include "db/database.h"
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

TEST_CASE("db lock: one holder at a time, and the next one learns who", "[db][lock][s3]")
{
    const std::string db = "/tmp/svlsp_test_lock/a.db";
    fs::remove_all("/tmp/svlsp_test_lock");
    fs::create_directories("/tmp/svlsp_test_lock");
    CHECK(DbLock::holder(db) == DbLock::Holder::None);

    DbLock first;
    REQUIRE(first.acquire(db, "build-db"));
    int pid = 0;
    CHECK(DbLock::holder(db, &pid) == DbLock::Holder::Builder);
    CHECK(pid == ::getpid());

    DbLock second; // another open file: flock conflicts within a process too
    CHECK_FALSE(second.acquire(db, "server"));
    first.release();
    CHECK(DbLock::holder(db) == DbLock::Holder::None);
    REQUIRE(second.acquire(db, "server"));
    CHECK(DbLock::holder(db) == DbLock::Holder::Server);
}

TEST_CASE("database: a copy of a file, WAL on a file, reopen read-only", "[db][lock][s3]")
{
    const std::string path = "/tmp/svlsp_test_lock/copy.db";
    fs::create_directories("/tmp/svlsp_test_lock");
    fs::remove(path);
    {
        Database file(path);
        file.useWal();
        file.initSchema();
        file.execute("INSERT INTO files (path, content_hash) VALUES ('/x.sv', 'h')");
        auto mode = file.prepare("PRAGMA journal_mode");
        REQUIRE(mode.step());
        CHECK(mode.columnText(0) == "wal");
    }
    Database mem(":memory:");
    mem.copyFrom(path);
    auto row = mem.prepare("SELECT path FROM files");
    REQUIRE(row.step());
    CHECK(row.columnText(0) == "/x.sv");
    mem.execute("DELETE FROM files"); // the copy only

    Database ro(":memory:");
    ro.reopen(path, /*readOnly=*/true);
    CHECK(ro.readOnly());
    auto count = ro.prepare("SELECT COUNT(*) FROM files");
    REQUIRE(count.step());
    CHECK(count.columnInt(0) == 1);
    CHECK_THROWS(ro.execute("DELETE FROM files"));
}
