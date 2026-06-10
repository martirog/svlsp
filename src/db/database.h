#pragma once
#include <sqlite3.h>
#include <cstdint>
#include <stdexcept>
#include <string>

// RAII wrapper around a SQLite3 connection.
// Use ":memory:" as path for an in-memory database (unit tests, transient state).
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&)                 = delete;
    Database& operator=(Database&&)      = delete;

    // Initialize the schema to SCHEMA_VERSION, running migrations as needed.
    void initSchema();

    // Return stored schema version (0 if the schema_version table is absent).
    int schemaVersion() const;

    // Execute a SQL statement that returns no rows (DDL, INSERT, DELETE, UPDATE).
    // Throws std::runtime_error on failure.
    void execute(const std::string& sql);

    // Lightweight RAII prepared statement.
    class Statement {
    public:
        Statement(sqlite3* db, const std::string& sql);
        ~Statement();

        Statement(const Statement&)            = delete;
        Statement& operator=(const Statement&) = delete;

        // Bind parameters (1-indexed, matching SQLite convention).
        Statement& bind(int idx, int val);
        Statement& bind(int idx, int64_t val);
        Statement& bind(int idx, const std::string& val);

        // Advance to the next row.  Returns true while a row is available.
        bool step();

        // Read a column from the current row.
        int64_t     columnInt(int idx)  const;
        std::string columnText(int idx) const;

        // Reset the statement so it can be re-executed (bindings are cleared).
        void reset();

    private:
        sqlite3*      m_db;
        sqlite3_stmt* m_stmt{nullptr};

        void check(int rc, const char* context) const;
    };

    Statement prepare(const std::string& sql) const;

    // Row id of the last successful INSERT on this connection.
    int64_t lastInsertRowId() const;

private:
    sqlite3* m_db{nullptr};

    void check(int rc, const char* context) const;
};
