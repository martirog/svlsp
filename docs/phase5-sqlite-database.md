# Phase 5 — SQLite Database Layer

**Goal:** Persist parsed symbols and diagnostics in a SQLite database so the
LSP providers can query them with structured SQL instead of scanning in-memory
structures, and so incremental compilation can skip re-parsing unchanged files
across requests.

Each sub-phase follows the pattern: unit test → implementation → two commits.

---

## 5.1 Schema Design

**Status:** Complete

### What was added

`src/db/schema.h` — compile-time SQL constants:

```cpp
inline constexpr int SCHEMA_VERSION = 2;    // current version
inline constexpr const char* SCHEMA_DDL = …;          // CREATE TABLE / INDEX
inline constexpr const char* MIGRATION_V1_TO_V2 = …;  // ALTER TABLE adds end_line, scope
```

Three tables:

```sql
CREATE TABLE files (
    id           INTEGER PRIMARY KEY,
    path         TEXT UNIQUE NOT NULL,
    content_hash TEXT NOT NULL,
    parsed_at    INTEGER NOT NULL
);

CREATE TABLE symbols (
    id       INTEGER PRIMARY KEY,
    file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    kind     TEXT NOT NULL,
    name     TEXT NOT NULL,
    line     INTEGER NOT NULL,
    col      INTEGER NOT NULL,
    parent   TEXT NOT NULL DEFAULT '',
    detail   TEXT NOT NULL DEFAULT '',
    end_line INTEGER NOT NULL DEFAULT 0,  -- last line of scope body; 0 for leaf symbols
    scope    TEXT NOT NULL DEFAULT ''     -- full enclosing scope chain, e.g. "MyModule::MyClass"
);

CREATE TABLE diagnostics (
    id      INTEGER PRIMARY KEY,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    line    INTEGER NOT NULL,
    col     INTEGER NOT NULL,
    message TEXT NOT NULL
);

CREATE TABLE schema_version (version INTEGER NOT NULL);
```

Indexes:

| Index | Columns | Purpose |
|---|---|---|
| `idx_symbols_name` | `name` | `findSymbolsByName`, `findSymbolsByNamePrefix` |
| `idx_symbols_file_id` | `file_id` | `symbolsForFile`, `replaceSymbols` |
| `idx_diagnostics_file_id` | `file_id` | `diagnosticsForFile` |
| `idx_symbols_scope` | `scope` | `findSymbolsInScope` |
| `idx_symbols_scope_name` | `(scope, name)` | scope-filtered prefix search |
| `idx_symbols_file_line` | `(file_id, line, end_line)` | `scopeAtPosition` range query |

Foreign keys use `ON DELETE CASCADE` — deleting a file row automatically cleans
up its symbols and diagnostics.

### Schema versioning

`schema_version` table holds a single integer row. `Database::initSchema()`:

1. Calls `schemaVersion()` → reads the table; returns 0 if absent.
2. If 0: creates all tables from `SCHEMA_DDL`, inserts version row.
3. If < 2: runs `MIGRATION_V1_TO_V2` (adds `end_line`, `scope` columns and three indexes).

This means the DB upgrades itself on first open after a binary update.

---

## 5.2 Database Abstraction Layer

**Status:** Complete

### What was added

`src/db/database.h/.cpp` — RAII wrapper around `sqlite3*`:

```cpp
class Database {
public:
    explicit Database(const std::string& path);  // ":memory:" for tests
    void    initSchema();
    int     schemaVersion() const;
    void    execute(const std::string& sql);
    Statement prepare(const std::string& sql);
    int64_t lastInsertRowId() const;
};
```

`Statement` is a move-only RAII handle to a `sqlite3_stmt*`:

```cpp
Statement& bind(int idx, int value);
Statement& bind(int idx, int64_t value);
Statement& bind(int idx, const std::string& value);
bool       step();          // returns true while rows remain
int64_t    columnInt(int i) const;
std::string columnText(int i) const;
void        reset();        // reuse without re-prepare
```

All methods throw `std::runtime_error` on SQLite error codes.

### Tests

`tests/unit/db/test_database.cpp` — 13 unit tests covering: open in-memory DB,
`initSchema` is idempotent, `schemaVersion` returns correct version, `execute`
and `prepare/step` round-trips, error propagation.

### CMake

```
find_package(SQLite3 QUIET)
```
Falls back to FetchContent amalgamation 3.47.0 from sqlite.org if the system
package is absent. Builds as `svlsp_sqlite3` static library.

`project(LANGUAGES CXX C)` is required for the amalgamation `.c` file.
`target_link_libraries(svlsp_sqlite3 PUBLIC ${CMAKE_DL_LIBS})` is needed for
`dlsym` on Linux.

---

## 5.3 Query API (SymbolDatabase)

**Status:** Complete

### What was added

`src/db/symbol_database.h/.cpp` — typed access layer:

```cpp
struct SymbolRow {
    int64_t     id;
    std::string kind;       // "Module", "Class", "Port", …
    std::string name;
    int         line;       // 1-based
    int         col;        // 0-based
    std::string parent;
    std::string detail;
    std::string filePath;
    int         endLine;    // last line of scope body; 0 for leaf symbols
    std::string scope;      // full enclosing scope chain
};

struct DiagnosticRow { int line; int col; std::string message; std::string filePath; };
```

Core write methods:

| Method | Description |
|---|---|
| `upsertFile(path, hash) → int64_t` | UPDATE then INSERT if not found; stable id for same path |
| `getFileHash(path) → string` | Returns `""` for unknown paths |
| `replaceSymbols(fileId, records)` | `BEGIN` → DELETE → INSERT × N → `COMMIT` |
| `replaceDiagnostics(fileId, errors)` | `BEGIN` → DELETE → INSERT × N → `COMMIT` |

Core read methods:

| Method | SQL strategy |
|---|---|
| `symbolsForFile(path)` | `WHERE file_id = ? ORDER BY line` |
| `findSymbolsByName(name)` | `WHERE s.name = ? ORDER BY f.path, s.line` |
| `diagnosticsForFile(path)` | `WHERE file_id = ? ORDER BY line` |

Context-aware read methods (added for Phase 6.1):

| Method | SQL strategy |
|---|---|
| `findSymbolsInScope(scope)` | `WHERE s.scope = ? ORDER BY s.name` |
| `findSymbolsByNamePrefix(prefix)` | `WHERE s.name LIKE ? ESCAPE '\'` (appends `%`) |
| `scopeAtPosition(path, line)` | Innermost scope-defining symbol where `line BETWEEN s.line AND s.end_line`, ordered by `length(scope) DESC` |
| `findSymbolsVisibleAt(path, line)` | UNION ALL of file-local in-scope symbols + cross-file top-level symbols; C++ sort by scope depth then name (see note below) |

**UNION ALL ORDER BY limitation:** SQLite only permits bare column names (not
expressions like `length(scope)`) in `ORDER BY` after a compound SELECT.
`findSymbolsVisibleAt` therefore omits `ORDER BY` from the SQL and sorts the
result with `std::sort` in C++, deepest scope first, then alphabetically by name.

### Tests

`tests/unit/db/test_symbol_database.cpp` — 15 unit tests covering: upsert,
hash lookup, replaceSymbols round-trip, findSymbolsByName cross-file, scope
queries, `scopeAtPosition` range detection, `findSymbolsVisibleAt` UNION logic.

---

## 5.4 Incremental Compilation Controller

**Status:** Complete

### What was added

`src/db/compilation_controller.h/.cpp`:

```cpp
class CompilationController {
public:
    explicit CompilationController(SymbolDatabase& sdb);
    std::vector<ParseError> compile(const std::string& path,
                                    const std::string& text);
private:
    static std::string hashContent(const std::string& text);
};
```

`compile(path, text)`:

1. Computes `hashContent(text)` → `std::to_string(std::hash<string>{}(text))`.
2. Calls `sdb.getFileHash(path)`.
3. **Cache hit** (hashes match): returns diagnostics from `sdb.diagnosticsForFile(path)` — no re-parse.
4. **Cache miss**: runs `CompilerDirectiveStripper` → `SvPreprocessor` → `SvTreeWalker`; calls `sdb.upsertFile`, `sdb.replaceSymbols`, `sdb.replaceDiagnostics`; returns fresh errors.

`LanguageServer` updated: `ParseCache` removed; `m_db` (`:memory:`), `m_symbolDb`,
`m_compiler` added as members (construction order matters — `m_db` must precede `m_symbolDb`
which must precede `m_compiler`). `parseDiagnostics()` delegates to `m_compiler.compile()`.

### Tests

`tests/unit/db/test_compilation_controller.cpp` — 9 unit tests covering: fresh
compile populates DB, same content hits cache, changed content recompiles, parse
errors stored and retrieved, symbols updated on recompile.

### Build issues resolved during Phase 5

1. **Circular dependency** — `compilation_controller.cpp` (in `svlsp_db`) used compiler
   sources that were in `svlsp_lib`, but `svlsp_lib` also linked `svlsp_db`. Fixed by
   extracting compiler sources into a new `svlsp_compiler` static lib.
2. **ODR violation in tests** — both `test_symbol_database.cpp` and
   `test_compilation_controller.cpp` defined `struct Fixture` at global scope, causing
   ASan/UBSan to select the wrong constructor. Fixed by wrapping both in `namespace {}`.
3. **C compiler** — SQLite amalgamation `.c` file requires `project(LANGUAGES CXX C)`.
4. **`dlsym`** — `target_link_libraries(svlsp_sqlite3 PUBLIC ${CMAKE_DL_LIBS})` needed on Linux.
5. **Stale CMakeCache** — after adding `CMAKE_C_COMPILER gcc-13` to `CMakePresets.json`,
   the old cache had to be deleted before the new compiler setting took effect.

### Library dependency graph

```
svlsp_compiler  (compiler_directive_stripper, sv_preprocessor, sv_tree_walker, parse_cache)
    → svlsp_antlr4

svlsp_db  (database, symbol_database, compilation_controller)
    → svlsp_sqlite3
    → svlsp_compiler

svlsp_lib  (lsp/* — server, providers, symbol_utils)
    → lsp
    → svlsp_compiler
    → svlsp_db
```

### Unit test totals after Phase 5

**211 tests, 471 assertions**
