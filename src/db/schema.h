#pragma once

namespace db {

// Current schema version.  Increment and add a migration in Database::initSchema
// whenever the schema changes.
inline constexpr int SCHEMA_VERSION = 1;

// DDL executed on a fresh (version-0) database.
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
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    kind    TEXT    NOT NULL,
    name    TEXT    NOT NULL,
    line    INTEGER NOT NULL,
    col     INTEGER NOT NULL,
    parent  TEXT    NOT NULL DEFAULT '',
    detail  TEXT    NOT NULL DEFAULT ''
);

CREATE INDEX idx_symbols_name    ON symbols(name);
CREATE INDEX idx_symbols_file_id ON symbols(file_id);

CREATE TABLE diagnostics (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    line    INTEGER NOT NULL,
    col     INTEGER NOT NULL,
    message TEXT    NOT NULL
);

CREATE INDEX idx_diagnostics_file_id ON diagnostics(file_id);
)sql";

} // namespace db
