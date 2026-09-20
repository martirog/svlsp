#pragma once
#include "db/database.h"
#include "compiler/parse_record.h"
#include <cstdint>
#include <optional>
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

struct InstantiationRow {
    int64_t     fileId;
    std::string filePath;
    int         line;
};

// Typed access layer over the svlsp SQLite schema.
// All methods operate on the database reference supplied at construction.
class SymbolDatabase {
public:
    explicit SymbolDatabase(Database& db);

    // ATTACHes each path in `paths` read-only onto this connection under a
    // generated alias ("lib0", "lib1", ...) so the cross-file symbol
    // queries below (findSymbolsByName, findSymbolsByNamePrefix,
    // findSymbolsInScope, findSymbolsVisibleAt) also search it -- the
    // "attach-and-query" shape for pre-built/shared library DBs (plan.md
    // §6.19), modeled on clangd's query-time MergedIndex rather than a
    // physical merge. A path already attached (by an earlier call, e.g. a
    // second discovered project sharing the same library) is skipped, not
    // re-attached under a second alias. Throws (via Database::prepare/
    // Statement::step) if `path` can't be opened, or if attaching would
    // exceed SQLite's default 10-attached-databases-per-connection limit.
    //
    // Deliberately NOT extended to: scopeAtPosition/scopeKindAtPosition/
    // enclosingClassNameAt (a (path, line) cursor position is always inside
    // a project's own edited file, never a read-only attached library
    // file); unresolvedInstantiatedTypeNames/instantiationsOfType (module/
    // interface instantiation resolution against an attached DB -- library
    // content reused this way is expected to be import'd, not
    // instantiated); or export-chain traversal inside findSymbolsVisibleAt
    // (collectExportedImports/fileIdForPackage only ever look in this
    // connection's own main schema, so `export pkg::*` re-exporting a
    // package that itself lives in an attached DB isn't followed) -- all
    // disclosed limitations for this first cut, not oversights.
    void attachLibraryDbs(const std::vector<std::string>& paths);

    // Persists `dirs` (in order) into *this connection's own main schema's*
    // library_include_dirs table -- overwriting whatever was stored there
    // before. Called once by LibraryDbBuilder::build, right after building
    // a library DB, so the DB file itself records the includeDirs it was
    // built with (plan.md §6.19 piece 4). Never called by the live server's
    // own :memory: DB -- there's nothing for it to record.
    void setLibraryIncludeDirs(const std::vector<std::string>& dirs);

    // Reads back whatever setLibraryIncludeDirs stored on *this
    // connection's own main schema* (never an attached one -- this is meant
    // to be called on a throwaway SymbolDatabase opened directly against a
    // library .db file, not the live project's own connection), in original
    // order. Returns {} if the table doesn't exist at all (a DB built
    // before this feature existed) or nothing was ever stored -- both
    // treated as "this library has no includeDirs to contribute," not an
    // error.
    std::vector<std::string> libraryIncludeDirs() const;

    // Persists `version` (e.g. SVLSP_GIT_VERSION) into *this connection's own
    // main schema's* library_build_info table -- overwriting whatever was
    // stored there before. Called once by LibraryDbBuilder::build, right
    // after building a library DB, so the DB file records which svlsp build
    // produced it. Never called by the live server's own :memory: DB.
    void setBuiltByVersion(const std::string& version);

    // Reads back whatever setBuiltByVersion stored on *this connection's own
    // main schema*. Returns "" if the table doesn't exist (a DB built before
    // this feature existed) or nothing was ever stored -- both treated as
    // "unknown version," not an error.
    std::string builtByVersion() const;

    // Forces every subsequent CompilationController::compile call against
    // this connection to be a cache miss (full reparse), by deleting every
    // row from `files` -- ON DELETE CASCADE takes symbols/diagnostics/
    // imports/instantiations with it. Used by LibraryDbBuilder::build when
    // builtByVersion() doesn't match the svlsp binary about to rebuild the
    // DB: the per-file content-hash cache alone can't detect that the
    // *parser itself* changed, only that file content didn't.
    void resetAllFiles();

    // Insert or update the file record for `path`, recording `contentHash`.
    // Returns the file_id (stable across calls for the same path).
    int64_t upsertFile(const std::string& path, const std::string& contentHash);

    // Return the stored content hash for `path`, or "" if the file is unknown.
    std::string getFileHash(const std::string& path) const;

    // Every path in `files`, in no particular order. Used by
    // ReferencesProvider/RenameProvider (plan.md item 3) to scan every file
    // the DB knows about for a name's occurrences -- there is no reference-
    // tracking table, only declarations, so those providers fall back to a
    // lexical, cross-file text search over this file set.
    std::vector<std::string> allFilePaths() const;

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

    // Every instantiation of `typeName`, with the referencing file's id/path
    // and the instantiation's line — used to attach a diagnostic to each
    // referencing file when `typeName` can't be resolved by LibraryResolver.
    std::vector<InstantiationRow> instantiationsOfType(const std::string& typeName) const;

    // Insert-only: appends diagnostics without deleting existing rows for
    // `fileId` (unlike replaceDiagnostics, which is delete-then-insert).
    void appendDiagnostics(int64_t fileId, const std::vector<ParseError>& extra);

    // Delete all file_includes rows where `fileId` is the includer, then
    // insert one row per path in `includedPaths` (each already upserted into
    // `files` by the caller). Records "fileId's own compiled unit
    // transitively includes each of these files" (plan.md §6.4) — called on
    // every real recompile, never on a cache hit.
    void replaceFileIncludes(int64_t fileId, const std::vector<std::string>& includedPaths);

    // Every file whose own compiled unit `` `include ``s `path` — the
    // reverse of replaceFileIncludes, used to find who needs a forced
    // recompile when `path` itself changes (plan.md §6.4). {} if nothing
    // includes `path`, or `path` is unknown.
    std::vector<std::string> includersOf(const std::string& path) const;

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

    // Returns the ParseRecordKind string (e.g. "Module", "Class", "Function")
    // of the innermost scope-defining symbol that contains `line` in `path`.
    // Returns "" when the position is outside all named scopes (top level /
    // compilation unit). Sibling of scopeAtPosition, same query shape, used
    // by keyword completion's context-legality check (src/lsp/sv_keywords.h).
    std::string scopeKindAtPosition(const std::string& path, int line) const;

    // Returns the bare name of the nearest enclosing `Class` scope that
    // contains `line` in `path` -- unlike scopeAtPosition/scopeKindAtPosition
    // (which report the innermost scope of *any* kind), this specifically
    // finds the nearest Class ancestor even when the cursor is nested inside
    // one of its methods (innermost scope kind there is Function, not
    // Class). "" when no enclosing class exists. Powers `this`/`super`
    // resolution in chained dot-completion (plan.md §6.14).
    std::string enclosingClassNameAt(const std::string& path, int line) const;

    // All symbols visible from `(path, line)`: every symbol in the scope chain
    // at that position (local → enclosing scopes) plus all top-level symbols
    // from every file.  Ordered innermost-scope-first, then by name.
    std::vector<SymbolRow> findSymbolsVisibleAt(const std::string& path, int line) const;

    // `className`'s own `extends` ancestor chain (plan.md §6.26): starts
    // with `className`'s own fully-qualified scope name ("MyPkg::MyClass"
    // form), then walks outward one class at a time via each Class symbol's own single
    // recorded parent name (`detail` -- see enterClass_declaration/
    // enterInterface_class_declaration, sv_tree_walker.cpp; no dedicated
    // inheritance-edge table exists yet). Returns {} if `className` isn't a
    // known Class at all -- a package name, an unresolved typedef alias
    // (e.g. UVM's own `typedef uvm_object_registry#(T) type_id;` idiom), or
    // a genuine unknown -- callers rely on this emptiness to tell "known
    // class, safe to walk its hierarchy" apart from "not a class, don't
    // guess," per this section's fail-closed design. A same-named-class
    // collision is broken by a same-file-preferred tie-break (`curPath`) --
    // the simpler half of `lsp::pickBestSymbol`, duplicated here rather than
    // shared since svlsp_db has no dependency on svlsp_lib (matching
    // compilation_controller.cpp's own pickCallee precedent). A cycle guard
    // stops the walk if a class ever (directly or transitively) names
    // itself as its own ancestor, rather than looping forever.
    std::vector<std::string> baseClassChain(const std::string& className,
                                             const std::string& curPath) const;

    // Resolves `methodName` as a Function/Task declared directly on
    // `className`'s own scope or, failing that, on the nearest ancestor
    // that declares it (walking baseClassChain outward) -- plan.md §6.26.
    // Returns nullopt if `className` isn't a known class, or neither it nor
    // any ancestor declares `methodName` as a Function/Task. Deliberately
    // never falls back to a name-only search across unrelated classes on a
    // miss -- that fallback is exactly the false-positive bug this section
    // fixes; callers that want a flat fallback for a genuinely unscoped
    // call do so themselves, only when there's no class context at all.
    std::optional<SymbolRow> resolveMethod(const std::string& className,
                                            const std::string& methodName,
                                            const std::string& curPath) const;

private:
    Database& m_db;
    std::vector<std::string> m_attachedLibraryPaths; // dedup guard for attachLibraryDbs

    // Runs "SELECT <symbol cols> FROM symbols s JOIN files f ON f.id=s.file_id
    // WHERE <cond>" (cond must reference only s/f and exactly one `?`)
    // against this connection's main schema, UNION ALL'd with the same
    // shape against every attached library schema (plan.md §6.19),
    // `bindValue` bound identically in each arm. No ORDER BY -- like
    // findSymbolsVisibleAt's own established precedent, callers sort the
    // result in C++ rather than relying on ORDER BY across a UNION ALL.
    std::vector<SymbolRow> queryAcrossAttachedDbs(
        const std::string& cond, const std::string& bindValue) const;

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
