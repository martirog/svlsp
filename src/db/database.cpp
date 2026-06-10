#include "db/database.h"
#include "db/schema.h"
#include <sqlite3.h>

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

Database::Database(const std::string& path)
{
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::string msg = "sqlite3_open: ";
        if (m_db) msg += sqlite3_errmsg(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        throw std::runtime_error(msg);
    }
    // Enable foreign key constraints for this connection.
    sqlite3_exec(m_db, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
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
    }
    // Future migrations: else if (v < 2) { /* ALTER TABLE ... */ }
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
