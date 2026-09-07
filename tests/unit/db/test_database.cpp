#include <catch2/catch_test_macros.hpp>
#include "db/database.h"

// ---------------------------------------------------------------------------
// Phase 5.2 — Database abstraction layer
// All tests use in-memory databases so they are hermetic and fast.
// ---------------------------------------------------------------------------

TEST_CASE("open in-memory database succeeds", "[db][database]") {
    CHECK_NOTHROW(Database(":memory:"));
}

TEST_CASE("initSchema creates tables and sets current version", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    CHECK(db.schemaVersion() == 6);
}

TEST_CASE("initSchema is idempotent", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    CHECK_NOTHROW(db.initSchema());
    CHECK(db.schemaVersion() == 6);
}

TEST_CASE("schemaVersion returns 0 on fresh database", "[db][database]") {
    Database db(":memory:");
    CHECK(db.schemaVersion() == 0);
}

TEST_CASE("execute runs DDL without error", "[db][database]") {
    Database db(":memory:");
    CHECK_NOTHROW(db.execute("CREATE TABLE t (x INTEGER)"));
}

TEST_CASE("execute throws on bad SQL", "[db][database]") {
    Database db(":memory:");
    CHECK_THROWS(db.execute("NOT VALID SQL"));
}

TEST_CASE("prepare and step return a row", "[db][database]") {
    Database db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");
    db.execute("INSERT INTO t VALUES (42)");
    auto stmt = db.prepare("SELECT x FROM t");
    REQUIRE(stmt.step());
    CHECK(stmt.columnInt(0) == 42);
    CHECK(!stmt.step());
}

TEST_CASE("bind int replaces placeholder", "[db][database]") {
    Database db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");
    db.execute("INSERT INTO t VALUES (7)");
    auto stmt = db.prepare("SELECT x FROM t WHERE x = ?");
    stmt.bind(1, 7);
    REQUIRE(stmt.step());
    CHECK(stmt.columnInt(0) == 7);
}

TEST_CASE("bind string replaces placeholder", "[db][database]") {
    Database db(":memory:");
    db.execute("CREATE TABLE t (s TEXT)");
    db.execute("INSERT INTO t VALUES ('hello')");
    auto stmt = db.prepare("SELECT s FROM t WHERE s = ?");
    stmt.bind(1, std::string("hello"));
    REQUIRE(stmt.step());
    CHECK(stmt.columnText(0) == "hello");
}

TEST_CASE("statement reset allows re-execution", "[db][database]") {
    Database db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");
    db.execute("INSERT INTO t VALUES (1)");
    db.execute("INSERT INTO t VALUES (2)");
    auto stmt = db.prepare("SELECT x FROM t ORDER BY x");
    REQUIRE(stmt.step());
    CHECK(stmt.columnInt(0) == 1);
    stmt.reset();
    REQUIRE(stmt.step());
    CHECK(stmt.columnInt(0) == 1);  // back to first row after reset
}

TEST_CASE("lastInsertRowId returns correct id after INSERT", "[db][database]") {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, v TEXT)");
    auto stmt = db.prepare("INSERT INTO t (v) VALUES (?)");
    stmt.bind(1, std::string("first"));
    stmt.step();
    CHECK(db.lastInsertRowId() == 1);
    stmt.reset();
    stmt.bind(1, std::string("second"));
    stmt.step();
    CHECK(db.lastInsertRowId() == 2);
}

TEST_CASE("multiple rows iterated correctly", "[db][database]") {
    Database db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");
    for (int i = 1; i <= 5; ++i)
        db.execute("INSERT INTO t VALUES (" + std::to_string(i) + ")");
    auto stmt = db.prepare("SELECT x FROM t ORDER BY x");
    int expected = 1;
    while (stmt.step())
        CHECK(stmt.columnInt(0) == expected++);
    CHECK(expected == 6);
}

TEST_CASE("initSchema creates files, symbols and diagnostics tables", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // If these tables exist the INSERT won't throw
    CHECK_NOTHROW(db.execute(
        "INSERT INTO files (path, content_hash) VALUES ('a.sv', 'abc')"));
    auto fid = db.lastInsertRowId();
    CHECK_NOTHROW(db.execute(
        "INSERT INTO symbols (file_id,kind,name,line,col) VALUES ("
        + std::to_string(fid) + ",'Module','top',1,0)"));
    CHECK_NOTHROW(db.execute(
        "INSERT INTO diagnostics (file_id,line,col,message) VALUES ("
        + std::to_string(fid) + ",2,3,'oops')"));
}

TEST_CASE("initSchema creates library_include_dirs table", "[db][database]") {
    // Backs plan.md §6.19 piece 4 (SymbolDatabase::setLibraryIncludeDirs/
    // libraryIncludeDirs) -- schema v6.
    Database db(":memory:");
    db.initSchema();
    CHECK_NOTHROW(db.execute(
        "INSERT INTO library_include_dirs (ordinal, dir) VALUES (0, '/some/dir')"));
}
