#pragma once

namespace db {

// Current schema version.  Increment and add a migration in Database::initSchema
// whenever the schema changes.
inline constexpr int SCHEMA_VERSION = 11;

// DDL executed on a fresh (version-0) database — always reflects the latest schema.
inline constexpr const char* SCHEMA_DDL = R"sql(
CREATE TABLE schema_version (
    version INTEGER NOT NULL
);

CREATE TABLE files (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    path         TEXT    NOT NULL UNIQUE,
    content_hash TEXT    NOT NULL,
    parsed_at    INTEGER NOT NULL DEFAULT (strftime('%s','now'))
);

CREATE TABLE symbols (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    kind     TEXT    NOT NULL,
    name     TEXT    NOT NULL,
    line     INTEGER NOT NULL,
    col      INTEGER NOT NULL,
    parent   TEXT    NOT NULL DEFAULT '',
    detail   TEXT    NOT NULL DEFAULT '',
    end_line INTEGER NOT NULL DEFAULT 0,
    scope    TEXT    NOT NULL DEFAULT ''
);

-- Exact-name lookup (workspace/definition queries).
CREATE INDEX idx_symbols_name       ON symbols(name);
-- All symbols in a file (document-symbol and diagnostics queries).
CREATE INDEX idx_symbols_file_id    ON symbols(file_id);
-- All symbols whose direct enclosing scope is X (completion, hover context).
CREATE INDEX idx_symbols_scope      ON symbols(scope);
-- Scope + name together (context-aware exact lookup).
CREATE INDEX idx_symbols_scope_name ON symbols(scope, name);
-- Position queries: find which scope contains a given line.
CREATE INDEX idx_symbols_file_line  ON symbols(file_id, line, end_line);

CREATE TABLE diagnostics (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    line    INTEGER NOT NULL,
    col     INTEGER NOT NULL,
    message TEXT    NOT NULL
);

CREATE INDEX idx_diagnostics_file_id ON diagnostics(file_id);

CREATE TABLE imports (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id   INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    pkg_name  TEXT    NOT NULL,
    item      TEXT    NOT NULL,           -- symbol name, or "*" for wildcard import
    is_export INTEGER NOT NULL DEFAULT 0  -- 1 for `export pkg::item`, 0 for plain import
);

CREATE INDEX idx_imports_file_id ON imports(file_id);

CREATE TABLE instantiations (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id   INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    type_name TEXT    NOT NULL,
    inst_name TEXT    NOT NULL DEFAULT '',
    line      INTEGER NOT NULL
);

CREATE INDEX idx_instantiations_file_id   ON instantiations(file_id);
CREATE INDEX idx_instantiations_type_name ON instantiations(type_name);

-- Populated only by LibraryDbBuilder::build (plan.md §6.19 piece 4), never
-- by the live server's own :memory: DB: the includeDirs a library DB was
-- itself built with, baked into the DB file so a project referencing it via
-- libraryDbs can adopt them for its own `` `include `` resolution without
-- needing that library's source .f/.json config to still exist (or still
-- match) alongside the .db file -- multiple versions of a library DB can
-- then sit in one directory, each self-describing and independently
-- correct, rather than sharing one ambient sidecar config file.
CREATE TABLE library_include_dirs (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    ordinal INTEGER NOT NULL, -- preserves original search order
    dir     TEXT    NOT NULL
);

CREATE INDEX idx_library_include_dirs_ordinal ON library_include_dirs(ordinal);

-- Populated only by LibraryDbBuilder::build, never by the live server's own
-- :memory: DB (same "library DB self-description" rationale as
-- library_include_dirs above): which svlsp build (SVLSP_GIT_VERSION)
-- actually produced this DB. The per-file content-hash cache in
-- CompilationController has no way to know the *parser itself* changed
-- between two --build-db runs against the same output path -- re-running
-- --build-db with a fixed/upgraded binary against an already-populated DB
-- would otherwise silently skip every unchanged file and never apply the
-- fix. Checked at the start of a build; a mismatch (including "no row at
-- all", e.g. a DB built before this feature existed) forces a full
-- rebuild by clearing `files` (see SymbolDatabase::resetAllFiles) rather
-- than trusting the stale per-file hashes.
CREATE TABLE library_build_info (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    svlsp_version TEXT    NOT NULL
);

-- One row per (includer, included) pair: `includer_file_id`'s own compiled
-- unit transitively `` `include ``s `included_file_id`'s text (plan.md
-- §6.4). Populated by CompilationController::compile on every real recompile
-- (never on a cache hit) from the same already-computed, fully-transitive
-- `includedFiles` list the diagnostics-visibility fix (2026-09-12) uses --
-- one compile of a top-level file already discovers its *entire* include
-- tree, so a reverse lookup here needs no further recursion to find every
-- file that would need recompiling if `included_file_id` changes.
CREATE TABLE file_includes (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    includer_file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    included_file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE
);

CREATE INDEX idx_file_includes_includer ON file_includes(includer_file_id);
CREATE INDEX idx_file_includes_included ON file_includes(included_file_id);

-- `define macros (plan.md §6.29 part A). A table of their own, not symbols
-- rows: SV macro names are a separate namespace (`` `define WIDTH `` and
-- `localparam WIDTH` routinely coexist), so keeping them out of symbols
-- keeps every existing name lookup unaffected. One row per `define in an
-- active branch, attributed to the file it's written in. `params` holds
-- one entry per parameter, "NAME" or "NAME=default", separated by U+001F;
-- `body` is the macro body (continuation lines joined, trailing comment
-- stripped), shown by hover (schema v10). `doc` is the comment block
-- documenting the `define (plan.md §6.31, schema v11).
CREATE TABLE macros (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id          INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    name             TEXT    NOT NULL,
    line             INTEGER NOT NULL,
    col              INTEGER NOT NULL,
    is_function_like INTEGER NOT NULL DEFAULT 0,
    params           TEXT    NOT NULL DEFAULT '',
    body             TEXT    NOT NULL DEFAULT '',
    doc              TEXT    NOT NULL DEFAULT ''
);

CREATE INDEX idx_macros_name    ON macros(name);
CREATE INDEX idx_macros_file_id ON macros(file_id);

-- Doc comments of declarations (plan.md §6.31), one row per documented
-- symbol, keyed by the symbol's position rather than symbols.id (ids
-- collide across attached library DBs). Replaced with the file's symbols.
CREATE TABLE symbol_docs (
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    line    INTEGER NOT NULL,
    col     INTEGER NOT NULL,
    doc     TEXT    NOT NULL,
    PRIMARY KEY (file_id, line, col)
) WITHOUT ROWID;
)sql";

// SQL applied when migrating an existing v10 database to v11.
inline constexpr const char* MIGRATION_V10_TO_V11 = R"sql(
ALTER TABLE macros ADD COLUMN doc TEXT NOT NULL DEFAULT '';
CREATE TABLE IF NOT EXISTS symbol_docs (
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    line    INTEGER NOT NULL,
    col     INTEGER NOT NULL,
    doc     TEXT    NOT NULL,
    PRIMARY KEY (file_id, line, col)
) WITHOUT ROWID;
UPDATE schema_version SET version = 11;
)sql";

// SQL applied when migrating an existing v9 database to v10.
inline constexpr const char* MIGRATION_V9_TO_V10 = R"sql(
ALTER TABLE macros ADD COLUMN body TEXT NOT NULL DEFAULT '';
UPDATE schema_version SET version = 10;
)sql";

// SQL applied when migrating an existing v8 database to v9.
inline constexpr const char* MIGRATION_V8_TO_V9 = R"sql(
-- `define macros (plan.md §6.29 part A). A table of their own, not symbols
-- rows: SV macro names are a separate namespace (`` `define WIDTH `` and
-- `localparam WIDTH` routinely coexist), so keeping them out of symbols
-- keeps every existing name lookup unaffected. One row per `define in an
-- active branch, attributed to the file it's written in. `params` holds
-- one entry per parameter, "NAME" or "NAME=default", separated by U+001F.
CREATE TABLE IF NOT EXISTS macros (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id          INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    name             TEXT    NOT NULL,
    line             INTEGER NOT NULL,
    col              INTEGER NOT NULL,
    is_function_like INTEGER NOT NULL DEFAULT 0,
    params           TEXT    NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_macros_name    ON macros(name);
CREATE INDEX IF NOT EXISTS idx_macros_file_id ON macros(file_id);
UPDATE schema_version SET version = 9;
)sql";

// SQL applied when migrating an existing v7 database to v8.
inline constexpr const char* MIGRATION_V7_TO_V8 = R"sql(
CREATE TABLE IF NOT EXISTS file_includes (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    includer_file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    included_file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_file_includes_includer ON file_includes(includer_file_id);
CREATE INDEX IF NOT EXISTS idx_file_includes_included ON file_includes(included_file_id);
UPDATE schema_version SET version = 8;
)sql";

// SQL applied when migrating an existing v6 database to v7.
inline constexpr const char* MIGRATION_V6_TO_V7 = R"sql(
CREATE TABLE IF NOT EXISTS library_build_info (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    svlsp_version TEXT    NOT NULL
);
UPDATE schema_version SET version = 7;
)sql";

// SQL applied when migrating an existing v5 database to v6.
inline constexpr const char* MIGRATION_V5_TO_V6 = R"sql(
CREATE TABLE IF NOT EXISTS library_include_dirs (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    ordinal INTEGER NOT NULL,
    dir     TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_library_include_dirs_ordinal ON library_include_dirs(ordinal);
UPDATE schema_version SET version = 6;
)sql";

// SQL applied when migrating an existing v4 database to v5.
inline constexpr const char* MIGRATION_V4_TO_V5 = R"sql(
CREATE TABLE IF NOT EXISTS instantiations (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id   INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    type_name TEXT    NOT NULL,
    inst_name TEXT    NOT NULL DEFAULT '',
    line      INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_instantiations_file_id   ON instantiations(file_id);
CREATE INDEX IF NOT EXISTS idx_instantiations_type_name ON instantiations(type_name);
UPDATE schema_version SET version = 5;
)sql";

// SQL applied when migrating an existing v3 database to v4.
inline constexpr const char* MIGRATION_V3_TO_V4 = R"sql(
ALTER TABLE imports ADD COLUMN is_export INTEGER NOT NULL DEFAULT 0;
UPDATE schema_version SET version = 4;
)sql";

// SQL applied when migrating an existing v2 database to v3.
inline constexpr const char* MIGRATION_V2_TO_V3 = R"sql(
CREATE TABLE IF NOT EXISTS imports (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    pkg_name TEXT    NOT NULL,
    item     TEXT    NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_imports_file_id ON imports(file_id);
UPDATE schema_version SET version = 3;
)sql";

// SQL applied when migrating an existing v1 database to v2.
inline constexpr const char* MIGRATION_V1_TO_V2 = R"sql(
ALTER TABLE symbols ADD COLUMN end_line INTEGER NOT NULL DEFAULT 0;
ALTER TABLE symbols ADD COLUMN scope    TEXT    NOT NULL DEFAULT '';
CREATE INDEX IF NOT EXISTS idx_symbols_scope      ON symbols(scope);
CREATE INDEX IF NOT EXISTS idx_symbols_scope_name ON symbols(scope, name);
CREATE INDEX IF NOT EXISTS idx_symbols_file_line  ON symbols(file_id, line, end_line);
UPDATE schema_version SET version = 2;
)sql";

} // namespace db
