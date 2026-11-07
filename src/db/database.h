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

    // Closes this connection and opens `path` instead (plan.md §6.34 S3),
    // read-only when `readOnly`. Throws like the constructor. Anything
    // attached is gone; statements must not outlive the old connection.
    void reopen(const std::string& path, bool readOnly = false);

    // Replaces this connection's main database with a copy of the file at
    // `path` (SQLite's online backup; the file is only read). Throws if it
    // can't be read.
    void copyFrom(const std::string& path);

    // For a DB file: WAL journal and synchronous=NORMAL, so a commit costs
    // no fsync (plan.md §6.34 S0 F2) and readers see the last commit while
    // a writer works. A no-op on ":memory:".
    void useWal();

    // The path this connection was opened with.
    const std::string& path() const { return m_path; }
    bool readOnly() const { return m_readOnly; }

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

    // A transaction over a scope (plan.md §6.34 S5): a SAVEPOINT, so it
    // nests inside another one (and the savepoints SymbolDatabase's own
    // writes use nest inside it). commit() releases it; leaving the scope
    // without commit() rolls it back. A reader on another connection sees
    // all of it or none of it.
    class Transaction {
    public:
        explicit Transaction(Database& db, const char* name = "txn");
        ~Transaction();
        void commit();
        Transaction(const Transaction&)            = delete;
        Transaction& operator=(const Transaction&) = delete;

    private:
        Database&   m_db;
        std::string m_name;
        bool        m_open{true};
    };

    // Row id of the last successful INSERT on this connection.
    int64_t lastInsertRowId() const;

private:
    sqlite3* m_db{nullptr};
    std::string m_path;
    bool m_readOnly{false};

    void open(const std::string& path, bool readOnly);

    void check(int rc, const char* context) const;
};
