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
    CHECK(db.schemaVersion() == 19);
}

TEST_CASE("initSchema is idempotent", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    CHECK_NOTHROW(db.initSchema());
    CHECK(db.schemaVersion() == 19);
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
    CHECK(db.schemaVersion() == 19);
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
    CHECK(db.schemaVersion() == 19);
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
    CHECK(db.schemaVersion() == 19);
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
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT source, subject FROM diagnostics"));
}

TEST_CASE("migration to v13 adds symbols.access", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v12 database: symbols without access.
    db.execute("ALTER TABLE symbols DROP COLUMN access;"
               "UPDATE schema_version SET version = 12;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT access FROM symbols"));
}

TEST_CASE("migration to v14 adds imports.scope", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v13 database: imports without scope.
    db.execute("ALTER TABLE imports DROP COLUMN scope;"
               "UPDATE schema_version SET version = 13;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT scope FROM imports"));
}

TEST_CASE("migration to v15 adds symbols.end_col", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v14 database: symbols without end_col.
    db.execute("ALTER TABLE symbols DROP COLUMN end_col;"
               "UPDATE schema_version SET version = 14;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT end_col FROM symbols"));
}

TEST_CASE("migration to v16 adds include_sites and diagnostics.severity", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    // Reshape into a v15 database: no include_sites, diagnostics without severity.
    db.execute("DROP TABLE include_sites;"
               "ALTER TABLE diagnostics DROP COLUMN severity;"
               "UPDATE schema_version SET version = 15;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT primary_file_id, included_file_id, includer_file_id, "
                             "line, scope FROM include_sites"));
    CHECK_NOTHROW(db.prepare("SELECT severity FROM diagnostics"));
}

TEST_CASE("migration to v17 adds the files' identity columns (plan.md §6.34 S1)",
          "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    db.execute("INSERT INTO files (path, content_hash) VALUES ('/a.sv', 'old-hash')");
    // Reshape into a v16 database: files without size / mtime_ns / stat_ns / inputs_fp.
    db.execute("ALTER TABLE files DROP COLUMN size;"
               "ALTER TABLE files DROP COLUMN mtime_ns;"
               "ALTER TABLE files DROP COLUMN stat_ns;"
               "ALTER TABLE files DROP COLUMN inputs_fp;"
               "DROP TABLE include_directives;"
               "UPDATE schema_version SET version = 16;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    auto row = db.prepare("SELECT size, mtime_ns, stat_ns, inputs_fp FROM files");
    REQUIRE(row.step());
    CHECK(row.columnInt(0) == -1); // unknown: the next check reads the file
    CHECK(row.columnInt(1) == 0);
    CHECK(row.columnInt(2) == 0);
    CHECK(row.columnText(3).empty()); // no fingerprint: the next compile misses
    CHECK_NOTHROW(db.prepare("SELECT primary_file_id, from_path, name, resolved "
                             "FROM include_directives"));
}

TEST_CASE("a development v16 database without include_sites gets it", "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    db.execute("DROP TABLE include_sites;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT scope FROM include_sites"));
}

TEST_CASE("migration to v18 adds projects, units and stored checks (plan.md §6.34 S2)",
          "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    db.execute("DROP TABLE project_info; DROP TABLE project_units; DROP TABLE unit_checks;"
               "UPDATE schema_version SET version = 17;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    CHECK_NOTHROW(db.prepare("SELECT config_path, config_fp, svlsp_version FROM project_info"));
    CHECK_NOTHROW(db.prepare("SELECT config_path, file_id, origin FROM project_units"));
    CHECK_NOTHROW(db.prepare("SELECT file_id, kind, data FROM unit_checks"));
}

TEST_CASE("migration to v19 adds the diagnostics' end column (plan.md §6.35 P2)",
          "[db][database]") {
    Database db(":memory:");
    db.initSchema();
    db.execute("INSERT INTO files (path, content_hash) VALUES ('/a.sv', 'h');"
               "INSERT INTO diagnostics (file_id, line, col, message) VALUES (1, 3, 4, 'm');"
               "ALTER TABLE diagnostics DROP COLUMN end_col;"
               "UPDATE schema_version SET version = 18;");
    db.initSchema();
    CHECK(db.schemaVersion() == 19);
    auto row = db.prepare("SELECT end_col FROM diagnostics");
    REQUIRE(row.step());
    CHECK(row.columnInt(0) == 0); // a one-character range, as before
}
