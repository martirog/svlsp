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
    CHECK(db.schemaVersion() == 12);
}

TEST_CASE("initSchema is idempotent", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    CHECK_NOTHROW(db.initSchema());
    CHECK(db.schemaVersion() == 12);
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

TEST_CASE("initSchema creates library_build_info table", "[db][database]") {
    // Backs SymbolDatabase::setBuiltByVersion/builtByVersion, the
    // stale-cache guard LibraryDbBuilder::build uses -- schema v7.
    Database db(":memory:");
    db.initSchema();
    CHECK_NOTHROW(db.execute(
        "INSERT INTO library_build_info (svlsp_version) VALUES ('abc1234')"));
}

TEST_CASE("initSchema creates file_includes table", "[db][database]") {
    // Backs SymbolDatabase::replaceFileIncludes/includersOf, plan.md §6.4's
    // cross-file invalidation -- schema v8.
    Database db(":memory:");
    db.initSchema();
    db.execute("INSERT INTO files (path, content_hash) VALUES ('/a.sv', 'h1')");
    db.execute("INSERT INTO files (path, content_hash) VALUES ('/b.sv', 'h2')");
    CHECK_NOTHROW(db.execute(
        "INSERT INTO file_includes (includer_file_id, included_file_id) VALUES (1, 2)"));
}

TEST_CASE("migration to v9 adds the macros table (plan.md §6.29 part A)", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v8 database: no macros table.
    db.execute("DROP TABLE macros; UPDATE schema_version SET version = 8;");
    db.initSchema();
    CHECK(db.schemaVersion() == 12);
    auto chk = db.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='macros'");
    REQUIRE(chk.step());
    CHECK(chk.columnInt(0) == 1);
}

TEST_CASE("migration to v10 adds macros.body", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v9 database: macros without body.
    db.execute("DROP TABLE macros;"
               "CREATE TABLE macros (id INTEGER PRIMARY KEY AUTOINCREMENT, file_id INTEGER NOT NULL,"
               " name TEXT NOT NULL, line INTEGER NOT NULL, col INTEGER NOT NULL,"
               " is_function_like INTEGER NOT NULL DEFAULT 0, params TEXT NOT NULL DEFAULT '');"
               "UPDATE schema_version SET version = 9;");
    db.initSchema();
    CHECK(db.schemaVersion() == 12);
    CHECK_NOTHROW(db.prepare("SELECT body FROM macros"));
}

TEST_CASE("migration to v11 adds symbol_docs and macros.doc (plan.md §6.31)", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v10 database: no symbol_docs, macros without doc.
    db.execute("DROP TABLE symbol_docs; DROP TABLE macros;"
               "CREATE TABLE macros (id INTEGER PRIMARY KEY AUTOINCREMENT, file_id INTEGER NOT NULL,"
               " name TEXT NOT NULL, line INTEGER NOT NULL, col INTEGER NOT NULL,"
               " is_function_like INTEGER NOT NULL DEFAULT 0, params TEXT NOT NULL DEFAULT '',"
               " body TEXT NOT NULL DEFAULT '');"
               "UPDATE schema_version SET version = 10;");
    db.initSchema();
    CHECK(db.schemaVersion() == 12);
    CHECK_NOTHROW(db.prepare("SELECT doc FROM macros"));
    CHECK_NOTHROW(db.prepare("SELECT file_id, line, col, doc FROM symbol_docs"));
}

TEST_CASE("migration to v12 adds diagnostics.source and .subject", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v11 database: diagnostics without source/subject.
    db.execute("DROP TABLE diagnostics;"
               "CREATE TABLE diagnostics (id INTEGER PRIMARY KEY AUTOINCREMENT,"
               " file_id INTEGER NOT NULL, line INTEGER NOT NULL, col INTEGER NOT NULL,"
               " message TEXT NOT NULL);"
               "UPDATE schema_version SET version = 11;");
    db.initSchema();
    CHECK(db.schemaVersion() == 12);
    CHECK_NOTHROW(db.prepare("SELECT source, subject FROM diagnostics"));
}
