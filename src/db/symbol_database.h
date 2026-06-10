#pragma once
#include "db/database.h"
#include "compiler/parse_record.h"
#include <cstdint>
#include <string>
#include <vector>

// Typed rows returned by query methods.
struct SymbolRow {
    int64_t     id;
    std::string kind;
    std::string name;
    int         line;
    int         col;
    std::string parent;
    std::string detail;
    std::string filePath;
};

struct DiagnosticRow {
    int         line;
    int         col;
    std::string message;
    std::string filePath;
};

// Typed access layer over the svlsp SQLite schema.
// All methods operate on the database reference supplied at construction.
class SymbolDatabase {
public:
    explicit SymbolDatabase(Database& db);

    // Insert or update the file record for `path`, recording `contentHash`.
    // Returns the file_id (stable across calls for the same path).
    int64_t upsertFile(const std::string& path, const std::string& contentHash);

    // Return the stored content hash for `path`, or "" if the file is unknown.
    std::string getFileHash(const std::string& path) const;

    // Delete all symbols for `fileId` then insert `records` in a single
    // transaction.
    void replaceSymbols(int64_t fileId, const std::vector<ParseRecord>& records);

    // Delete all diagnostics for `fileId` then insert `errors`.
    void replaceDiagnostics(int64_t fileId, const std::vector<ParseError>& errors);

    // LSP query helpers (used by Phase-6 feature providers).
    std::vector<SymbolRow>     symbolsForFile(const std::string& path) const;
    std::vector<SymbolRow>     findSymbolsByName(const std::string& name) const;
    std::vector<DiagnosticRow> diagnosticsForFile(const std::string& path) const;

private:
    Database& m_db;

    // Returns the file_id for `path`, or -1 if not found.
    int64_t fileIdFor(const std::string& path) const;
};
