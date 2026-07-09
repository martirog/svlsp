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
    int         endLine; // last line of scope body; 0 for leaf symbols
    std::string scope;   // full enclosing scope chain, e.g. "MyModule::MyClass"
};

struct DiagnosticRow {
    int         line;
    int         col;
    std::string message;
    std::string filePath;
};

struct ImportRow {
    std::string pkgName;
    std::string item;      // symbol name, or "*" for wildcard
    bool        isExport{false};
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

    // Delete all imports for `fileId` then insert `imports`.
    void replaceImports(int64_t fileId, const std::vector<ImportRecord>& imports);

    // Delete all instantiations for `fileId` then insert `insts`.
    void replaceInstantiations(int64_t fileId, const std::vector<InstantiationRecord>& insts);

    // Distinct type names instantiated somewhere with no matching Module/
    // Interface/Program declaration anywhere in the DB. Drives library resolution.
    std::vector<std::string> unresolvedInstantiatedTypeNames() const;

    // Insert-only: appends diagnostics without deleting existing rows for
    // `fileId` (unlike replaceDiagnostics, which is delete-then-insert).
    void appendDiagnostics(int64_t fileId, const std::vector<ParseError>& extra);

    // LSP query helpers (used by Phase-6 feature providers).
    std::vector<SymbolRow>     symbolsForFile(const std::string& path) const;
    std::vector<SymbolRow>     findSymbolsByName(const std::string& name) const;
    std::vector<DiagnosticRow> diagnosticsForFile(const std::string& path) const;

    // All symbols whose direct enclosing scope equals `scope`
    // (pass "" for top-level symbols).  Ordered by name.
    std::vector<SymbolRow> findSymbolsInScope(const std::string& scope) const;

    // Cross-file prefix search: symbols whose name starts with `prefix`.
    // Used for workspace/symbol queries and completion filtering.
    std::vector<SymbolRow> findSymbolsByNamePrefix(const std::string& prefix) const;

    // Returns the full scope path (e.g. "MyModule::MyClass::myFunc") of the
    // innermost scope-defining symbol that contains `line` in `path`.
    // Returns "" when the position is outside all named scopes.
    std::string scopeAtPosition(const std::string& path, int line) const;

    // All symbols visible from `(path, line)`: every symbol in the scope chain
    // at that position (local → enclosing scopes) plus all top-level symbols
    // from every file.  Ordered innermost-scope-first, then by name.
    std::vector<SymbolRow> findSymbolsVisibleAt(const std::string& path, int line) const;

private:
    Database& m_db;

    int64_t fileIdFor(const std::string& path) const;
    std::vector<ImportRow> importsForFileId(int64_t fileId) const;

    // The file_id of the file that declares top-level package `pkgName`,
    // or -1 if no such package is known.
    int64_t fileIdForPackage(const std::string& pkgName) const;

    // Recursively resolves `export pkg::*` / `export pkg::item` declarations
    // reachable from `pkgName`, appending re-exported wildcard package names
    // and specific {pkg, name} imports. `visited` guards against export cycles.
    void collectExportedImports(
        const std::string& pkgName,
        std::vector<std::string>& outWildcardPkgs,
        std::vector<std::pair<std::string, std::string>>& outSpecific,
        std::vector<std::string>& visited) const;
};
