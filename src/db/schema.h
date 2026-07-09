#pragma once

namespace db {

// Current schema version.  Increment and add a migration in Database::initSchema
// whenever the schema changes.
inline constexpr int SCHEMA_VERSION = 5;

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
