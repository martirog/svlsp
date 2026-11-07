#include "db/database.h"
#include <algorithm>
#include "db/schema.h"
#include <sqlite3.h>

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

Database::Database(const std::string& path)
{
    open(path, false);
}

void Database::open(const std::string& path, bool readOnly)
{
    sqlite3* db = nullptr;
    const int flags = readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    int rc = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = "sqlite3_open: ";
        if (db) msg += sqlite3_errmsg(db);
        sqlite3_close(db);
        throw std::runtime_error(msg);
    }
    sqlite3_close(m_db);
    m_db = db;
    m_path = path;
    m_readOnly = readOnly;
    // Enable foreign key constraints for this connection.
    sqlite3_exec(m_db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
    // A writer (a --build-db, a server) may hold the file briefly: wait
    // rather than fail.
    sqlite3_busy_timeout(m_db, 5000);
}

void Database::reopen(const std::string& path, bool readOnly)
{
    open(path, readOnly);
}

void Database::copyFrom(const std::string& path)
{
    sqlite3* src = nullptr;
    if (sqlite3_open_v2(path.c_str(), &src, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::string msg = "sqlite3_open: ";
        if (src) msg += sqlite3_errmsg(src);
        sqlite3_close(src);
        throw std::runtime_error(msg);
    }
    sqlite3_busy_timeout(src, 5000);
    sqlite3_backup* backup = sqlite3_backup_init(m_db, "main", src, "main");
    if (!backup) {
        const std::string msg = std::string("sqlite3_backup_init: ") + sqlite3_errmsg(m_db);
        sqlite3_close(src);
        throw std::runtime_error(msg);
    }
    int rc;
    while ((rc = sqlite3_backup_step(backup, -1)) == SQLITE_OK || rc == SQLITE_BUSY ||
           rc == SQLITE_LOCKED)
        sqlite3_sleep(20);
    sqlite3_backup_finish(backup);
    sqlite3_close(src);
    if (rc != SQLITE_DONE)
        throw std::runtime_error("sqlite3_backup_step: " + std::string(sqlite3_errstr(rc)));
}

void Database::useWal()
{
    if (m_path == ":memory:" || m_path.empty() || m_readOnly) return;
    sqlite3_exec(m_db, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous = NORMAL", nullptr, nullptr, nullptr);
}

Database::~Database()
{
    sqlite3_close(m_db);
}

void Database::check(int rc, const char* context) const
{
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw std::runtime_error(std::string(context) + ": " + sqlite3_errmsg(m_db));
}

int Database::schemaVersion() const
{
    // Check if schema_version table exists.
    auto chk = prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type='table' AND name='schema_version'");
    chk.step();
    if (chk.columnInt(0) == 0) return 0;

    auto qv = prepare("SELECT version FROM schema_version LIMIT 1");
    if (qv.step()) return static_cast<int>(qv.columnInt(0));
    return 0;
}

void Database::initSchema()
{
    int v = schemaVersion();
    if (v == 0) {
        execute(db::SCHEMA_DDL);
        execute("INSERT INTO schema_version VALUES ("
                + std::to_string(db::SCHEMA_VERSION) + ")");
        return;
    }
    if (v < 2)
        execute(db::MIGRATION_V1_TO_V2);
    if (v < 3)
        execute(db::MIGRATION_V2_TO_V3);
    if (v < 4)
        execute(db::MIGRATION_V3_TO_V4);
    if (v < 5)
        execute(db::MIGRATION_V4_TO_V5);
    if (v < 6)
        execute(db::MIGRATION_V5_TO_V6);
    if (v < 7)
        execute(db::MIGRATION_V6_TO_V7);
    if (v < 8)
        execute(db::MIGRATION_V7_TO_V8);
    if (v < 9)
        execute(db::MIGRATION_V8_TO_V9);
    if (v < 10)
        execute(db::MIGRATION_V9_TO_V10);
    if (v < 11)
        execute(db::MIGRATION_V10_TO_V11);
    if (v < 12) {
        // Skip the ALTER when the column is already there (a database
        // reshaped to an older version by hand still has it).
        auto col = prepare("SELECT 1 FROM pragma_table_info('diagnostics') "
                           "WHERE name = 'source'");
        execute(col.step() ? "UPDATE schema_version SET version = 12;"
                           : db::MIGRATION_V11_TO_V12);
    }
    if (v < 13) {
        // Same guard as v12.
        auto col = prepare("SELECT 1 FROM pragma_table_info('symbols') WHERE name = 'access'");
        execute(col.step() ? "UPDATE schema_version SET version = 13;"
                           : db::MIGRATION_V12_TO_V13);
    }
    if (v < 14) {
        // Same guard as v12.
        auto col = prepare("SELECT 1 FROM pragma_table_info('imports') WHERE name = 'scope'");
        execute(col.step() ? "UPDATE schema_version SET version = 14;"
                           : db::MIGRATION_V13_TO_V14);
    }
    if (v < 15) {
        // Same guard as v12.
        auto col = prepare("SELECT 1 FROM pragma_table_info('symbols') WHERE name = 'end_col'");
        execute(col.step() ? "UPDATE schema_version SET version = 15;"
                           : db::MIGRATION_V14_TO_V15);
    }
    // Also when include_sites is missing: a development build's v16 had a
    // files.include_scope column instead.
    auto sites = prepare("SELECT 1 FROM sqlite_master WHERE type = 'table' "
                         "AND name = 'include_sites'");
    if (v < 16 || !sites.step()) {
        // Same guard as v12.
        auto col = prepare("SELECT 1 FROM pragma_table_info('diagnostics') "
                           "WHERE name = 'severity'");
        if (!col.step()) execute(db::MIGRATION_V16_SEVERITY);
        execute(db::MIGRATION_V15_TO_V16);
    }
    // The v16 repair above may have set the version back to 16.
    v = std::min(v, schemaVersion());
    if (v < 17) {
        // Same guard as v12.
        auto col = prepare("SELECT 1 FROM pragma_table_info('files') WHERE name = 'inputs_fp'");
        execute(col.step() ? "UPDATE schema_version SET version = 17;"
                           : db::MIGRATION_V16_TO_V17);
    }
    if (v < 18) execute(db::MIGRATION_V17_TO_V18);
}

void Database::execute(const std::string& sql)
{
    char* errmsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = "Database::execute: ";
        if (errmsg) { msg += errmsg; sqlite3_free(errmsg); }
        throw std::runtime_error(msg);
    }
}

Database::Statement Database::prepare(const std::string& sql) const
{
    return Statement{m_db, sql};
}

int64_t Database::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(m_db);
}

// ---------------------------------------------------------------------------
// Database::Statement
// ---------------------------------------------------------------------------

Database::Statement::Statement(sqlite3* db, const std::string& sql)
    : m_db{db}
{
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &m_stmt, nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error(
            std::string("sqlite3_prepare_v2: ") + sqlite3_errmsg(db));
}

Database::Statement::~Statement()
{
    sqlite3_finalize(m_stmt);
}

void Database::Statement::check(int rc, const char* context) const
{
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE)
        throw std::runtime_error(std::string(context) + ": " + sqlite3_errmsg(m_db));
}

Database::Statement& Database::Statement::bind(int idx, int val)
{
    check(sqlite3_bind_int(m_stmt, idx, val), "bind int");
    return *this;
}

Database::Statement& Database::Statement::bind(int idx, int64_t val)
{
    check(sqlite3_bind_int64(m_stmt, idx, val), "bind int64");
    return *this;
}

Database::Statement& Database::Statement::bind(int idx, const std::string& val)
{
    check(sqlite3_bind_text(m_stmt, idx, val.c_str(),
                            static_cast<int>(val.size()), SQLITE_TRANSIENT),
          "bind text");
    return *this;
}

bool Database::Statement::step()
{
    int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)  return true;
    if (rc == SQLITE_DONE) return false;
    throw std::runtime_error(
        std::string("sqlite3_step: ") + sqlite3_errmsg(m_db));
}

int64_t Database::Statement::columnInt(int idx) const
{
    return sqlite3_column_int64(m_stmt, idx);
}

std::string Database::Statement::columnText(int idx) const
{
    const unsigned char* t = sqlite3_column_text(m_stmt, idx);
    if (!t) return {};
    return reinterpret_cast<const char*>(t);
}

void Database::Statement::reset()
{
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
}
