# Phase 6 — End-to-End Integration and Polishing

**Goal:** Wire all layers together, replace the placeholder `nullptr` returns in
the Phase 3 LSP providers with real DB-backed queries, and run the full Emacs
functional test suite.

---

## 6.1 DB-Backed LSP Providers

**Status:** Complete

### Overview

Five of the eight LSP feature providers were rewritten to accept a
`SymbolDatabase&` (and, for position-based providers, the current document text)
and return real results drawn from the SQLite symbol database.

Before Phase 6.1 every provider returned `nullptr` (JSON null). After Phase 6.1:

| Provider | LSP method | Before | After |
|---|---|---|---|
| `DocumentSymbolsProvider` | `textDocument/documentSymbol` | null | symbol outline with ranges |
| `WorkspaceSymbolsProvider` | `workspace/symbol` | null | cross-file prefix search |
| `HoverProvider` | `textDocument/hover` | null | Markdown symbol info |
| `DefinitionProvider` | `textDocument/definition` | null | `Location` in source file |
| `CompletionProvider` | `textDocument/completion` | null | scope-aware candidates |
| `ReferencesProvider` | `textDocument/references` | null | null — Phase 6.2+ |
| `RenameProvider` | `textDocument/rename` | null | null — Phase 6.2+ |
| `SignatureHelpProvider` | `textDocument/signatureHelp` | null | null — Phase 6.2+ |

### What was added

#### `src/lsp/symbol_utils.h/.cpp`

Shared helpers used by all providers:

```cpp
lsp::SymbolKind         symbolKindFor(const std::string& kind);
lsp::CompletionItemKind completionKindFor(const std::string& kind);
std::string wordAtPosition(const std::string& text, unsigned line, unsigned character);
lsp::Range      makeRange(int line1, int col0, int nameLen);
lsp::DocumentUri pathToUri(const std::string& path);
```

**`wordAtPosition` semantics:** Finds the SystemVerilog identifier under or
immediately to the left of the cursor. Valid identifier characters are
`[a-zA-Z0-9_$]`. When the cursor sits on whitespace (e.g. just after typing a
word and pressing the completion key), it walks left to return the prefix already
typed — this is the correct behaviour for completion triggers.

**`makeRange`:** Converts 1-based compiler line numbers to 0-based LSP positions.
`col0` is already 0-based. Returns a range spanning the identifier:
`start = {line1-1, col0}`, `end = {line1-1, col0 + nameLen}`.

**Kind mappings:**

| DB kind | `SymbolKind` | `CompletionItemKind` |
|---|---|---|
| Module | Module | Module |
| Interface | Interface | Interface |
| Package | Package | Module |
| Class | Class | Class |
| Function | Function | Function |
| Task | Method | Function |
| Port | Field | Field |
| Signal | Variable | Variable |
| Parameter | Constant | Constant |
| Macro | Constant | Keyword |
| (unknown) | Variable | Text |

#### Document Symbols

```cpp
// src/lsp/document_symbols.h
static lsp::TextDocument_DocumentSymbolResult
getDocumentSymbols(const lsp::DocumentSymbolParams&, SymbolDatabase&);
```

Implementation:

1. Extracts the filesystem path from `params.textDocument.uri`.
2. Calls `db.symbolsForFile(path)` — returns null if the file is not in the DB.
3. Builds `lsp::Array<lsp::DocumentSymbol>`:
   - **Scope symbols** (endLine > 0): `range` spans from `{line-1, col}` to
     `{endLine-1, 0}`; `selectionRange` is the single-line identifier range.
   - **Leaf symbols** (endLine = 0): `range == selectionRange` (single line).
   - `detail` is set when the `ParseRecord::detail` field is non-empty.

#### Workspace Symbols

```cpp
// src/lsp/workspace_symbols.h
static lsp::TextDocument_WorkspaceSymbolResult
getWorkspaceSymbols(const lsp::WorkspaceSymbolParams&, SymbolDatabase&);
```

Implementation:

1. Calls `db.findSymbolsByNamePrefix(params.query)` — an empty query returns all symbols.
2. Builds `lsp::Array<lsp::WorkspaceSymbol>` with `Location{pathToUri, makeRange}`.

#### Hover

```cpp
// src/lsp/hover.h
static lsp::TextDocument_HoverResult
getHover(const lsp::HoverParams&, SymbolDatabase&, const std::string& docText);
```

Implementation:

1. `wordAtPosition(docText, line, character)` — returns null if no identifier found.
2. `db.findSymbolsByName(word)` — returns null if not in DB.
3. Prefers the result from the same file when multiple files define the same name.
4. Builds `lsp::Hover` with `MarkupContent{Markdown}`:
   ```
   **Kind** `name` → `detail`

   in *scope*
   ```
   The `→ detail` clause is omitted when `detail` is empty; the `in *scope*`
   line is omitted for top-level symbols (`scope == ""`).

#### Definition

```cpp
// src/lsp/definition.h
static lsp::TextDocument_DefinitionResult
getDefinition(const lsp::DefinitionParams&, SymbolDatabase&, const std::string& docText);
```

Implementation:

1. `wordAtPosition` → null if no identifier.
2. `db.findSymbolsByName(word)` → null if not in DB.
3. Returns `Definition{Location{pathToUri(row.filePath), makeRange(row.line, row.col, word.size())}}`.

#### Completion

```cpp
// src/lsp/completion.h
static lsp::TextDocument_CompletionResult
getCompletion(const lsp::CompletionParams&, SymbolDatabase&, const std::string& docText);
```

Implementation:

1. Checks `m_store.contains(uri)` in `server.cpp` before calling; returns null if not open.
2. `wordAtPosition(docText, line, character)` — the partial identifier the user has already typed.
3. `db.findSymbolsVisibleAt(path, line + 1)` — scope-aware: returns file-local symbols
   in the scope chain at the cursor line, plus top-level symbols from all other files.
4. Filters: any `row.name` that doesn't start with `prefix` is excluded (when prefix is non-empty).
5. Returns null if the visible-symbol list is empty or no items survive the filter.
6. Builds `lsp::Array<lsp::CompletionItem>` with `completionKindFor` and optional `detail`.

### Scope-aware completion: how `findSymbolsVisibleAt` works

```sql
-- Part 1: file-local symbols in any scope in the chain
SELECT … FROM symbols s JOIN files f ON f.id = s.file_id
WHERE f.path = ? AND s.scope IN (?, ?, …)   -- innermost…outermost, then ""

UNION ALL

-- Part 2: top-level symbols from all other files
SELECT … FROM symbols s JOIN files f ON f.id = s.file_id
WHERE f.path != ? AND s.scope = ''
```

The scope chain is built by `scopeAtPosition` then decomposed:

```
"MyModule::MyClass::myFunc"
  → ["MyModule::MyClass::myFunc", "MyModule::MyClass", "MyModule", ""]
```

Because SQLite forbids expressions like `length(scope)` in `ORDER BY` after
`UNION ALL`, the result is sorted in C++ — deepest scope first, then
alphabetically by name.

### Server wiring

`LanguageServer::registerHandlers()` in `src/lsp/server.cpp` was updated to
pass `m_symbolDb` and the document text to each provider lambda. Position-based
providers also guard with `m_store.contains()`:

```cpp
.add<lsp::requests::TextDocument_Hover>(
    [this](lsp::HoverParams&& params) {
        if (!m_store.contains(params.textDocument.uri))
            return lsp::TextDocument_HoverResult{nullptr};
        return HoverProvider::getHover(
            params, m_symbolDb, m_store.get(params.textDocument.uri).text);
    })
```

### `ParseRecord` schema additions

`src/compiler/parse_record.h` gained two fields with defaults so existing
positional aggregate initialisers still compile:

```cpp
int         endLine{0};  // 1-based last line of scope body; 0 for leaf symbols
std::string scope{};     // full enclosing scope chain, e.g. "MyModule::MyClass"
```

`SvTreeWalker` was updated with:
- `currentScopeChain()` — joins the scope stack with `::` separators
- `backpatchEndLine(name, endLine)` — scans `m_records` backward to patch
  the endLine of the named scope-defining symbol once its closing token is seen
- All six exit hooks (`exitModuleDeclaration`, `exitClassDeclaration`, etc.) call
  `backpatchEndLine(currentScope(), ctx->stop->getLine())` before `popScope()`

### Tests added

#### Unit tests

`tests/unit/lsp/test_symbol_utils.cpp`:
- `wordAtPosition`: multi-line text, out-of-range positions, `$`/`_` identifiers,
  cursor-on-whitespace walks left (completion semantics)
- `symbolKindFor`: all 10 known kinds, unknown falls back to Variable
- `makeRange`: 1-based → 0-based line conversion, character offsets

`tests/unit/lsp/test_document_symbols.cpp`:
- Null when file not in DB
- Returns symbols for open file; scope symbols have multi-line range
- Leaf symbols have `range == selectionRange`
- `detail` field populated when present

`tests/unit/lsp/test_workspace_symbols.cpp`:
- Null for empty DB; null when prefix matches nothing
- Returns matching symbols; empty query returns all
- Location points to correct file and line

`tests/unit/lsp/test_hover.cpp`:
- Null on non-identifier; null when not in DB
- Returns Markdown for module; scope line present for nested symbols
- Return-type detail included when set

`tests/unit/lsp/test_definition.cpp`:
- Null on non-identifier; null when not in DB
- Returns `Location` for known symbol; cross-file lookup works

`tests/unit/lsp/test_completion.cpp`:
- Null when no symbols visible; top-level symbols from all files returned
- Filters by typed prefix; items have correct `CompletionItemKind`

#### Integration tests updated

Tests 05, 06, 08, 09, 10 were rewritten from "expect null (pre-ANTLR4)" to
verify real provider behaviour using `examples/module_basic.sv` (module `adder`
at line 4, `adder` identifier at character 7):

| Test file | Old expectation | New expectation |
|---|---|---|
| `test_05_hover.sh` | null result | non-null Hover on `adder` (line=3, char=9); null on blank line |
| `test_06_definition.sh` | null result | non-null Location on `adder`; null on blank line |
| `test_08_completion.sh` | null result | non-null candidates inside module body (line=10); null outside any scope |
| `test_09_document_symbols.sh` | null result | non-null list for open file; null for unknown URI |
| `test_10_workspace_symbols.sh` | null result | non-null for query `"adder"`; null for `"zzz_no_match"` |

### Unit test totals after Phase 6.1

**228 tests, 550 assertions** (246/618 after Phase 6.3)

---

## 6.2 Multi-File Project Support

**Status:** Not started

Planned: `compile_commands.json` or custom `.svlsp.json` listing all source
files. Batch-compile at startup; background re-compile on change. See `plan.md §6.2`.

---

## 6.3 Package Import Resolution

**Status:** Complete

### Problem

`findSymbolsVisibleAt` was surfacing only `scope = ""` symbols from other files
(the package declarations themselves). Symbols *inside* a package — e.g. a class
with `scope = "util_pkg"` — remained invisible at the caller's scope even when
`import util_pkg::*` appeared at the top of the file.

### Design

Three-part extension to `findSymbolsVisibleAt`:

```sql
-- Part 1 (unchanged): file-local scope chain
SELECT … WHERE f.path = ? AND s.scope IN (?, …)

UNION ALL

-- Part 2: cross-file top-level + wildcard-imported package scopes
SELECT … WHERE f.path != ? AND s.scope IN ('', 'util_pkg', …)

UNION ALL

-- Part 3 (one arm per specific import): import pkg::Foo
SELECT … WHERE s.scope = 'util_pkg' AND s.name = 'Foo'
```

### New components

**`ImportRecord`** (`src/compiler/parse_record.h`):
```cpp
struct ImportRecord {
    std::string pkgName;   // package being imported
    std::string item;      // symbol name, or "*" for wildcard
    int         line{0};
    std::string file{};    // empty = primary compiled file
};
```

`WalkResult` gains a third field `imports`; `SvRecordListener::enterPackage_import_item`
fires on each `import` statement and pushes an `ImportRecord` (source-map translated).

**`imports` table** (`src/db/schema.h`, schema v3):
```sql
CREATE TABLE imports (
    id       INTEGER PRIMARY KEY,
    file_id  INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    pkg_name TEXT    NOT NULL,
    item     TEXT    NOT NULL
);
CREATE INDEX idx_imports_file_id ON imports(file_id);
```
`item = "*"` for wildcard imports. `replaceImports(file_id, imports)` is called by
`CompilationController::compile` alongside `replaceSymbols`.

**`SymbolDatabase`** gains:
- `void replaceImports(int64_t fileId, const std::vector<ImportRecord>&)`
- `std::vector<ImportRow> importsForFileId(int64_t fileId)` (private)
- `ImportRow { pkgName, item }`

### Tests

#### Unit tests (8 new cases)

- `test_sv_listener.cpp` (`[import]`): wildcard and specific imports parsed and
  emitted; source-map translation applies to import line numbers.
- `test_symbol_database.cpp` (`[import]`): wildcard makes package scope visible;
  specific import makes exactly that symbol visible; other-package symbols excluded;
  `replaceImports` clears and rewrites records atomically.
- `test_compilation_controller.cpp`: import records stored and replaced on recompile.

#### Integration tests (`test_17_import_resolution.sh`, 10 tests)

Fixtures:
- `util_pkg.sv` — `package util_pkg { class DataItem; class Logger; function compute }`
- `import_wildcard.sv` — `import util_pkg::*; module wildcard_user { DataItem item; Logger log_obj; }`
- `import_specific.sv` — `import util_pkg::DataItem; module specific_user { … }`

| Test | Assertion |
|---|---|
| prerequisite | util_pkg.sv opens and LSP initialises (seeds DB) |
| wildcard completion | includes DataItem |
| wildcard completion | includes Logger |
| wildcard completion | includes compute |
| wildcard hover | DataItem → non-null result |
| wildcard definition | DataItem URI ends with `util_pkg.sv` |
| wildcard definition | DataItem range.start.line = 2 (LSP 0-based) |
| specific completion | includes DataItem |
| specific completion | excludes Logger (not imported) |
| specific completion | excludes compute (not imported) |

### Unit test totals after Phase 6.3

**246 tests, 618 assertions**

---

## 6.4 Cross-File Invalidation

**Status:** Not started

When file A is recompiled and its symbols change, files that reference A's
symbols should be marked stale and re-checked.

Required work:
1. `file_dependencies (dependent_file_id, dependency_file_id)` table.
2. `CompilationController::compile` queries dependents and recompiles them.
3. Cycle guard using a visited set.

Integration tests: `test_18_cross_file_error_propagation.sh`,
`test_19_cross_file_error_cleared.sh` (not yet written). See `plan.md §6.4`.

---

## 6.5 Performance Baseline

**Status:** Not started

Measure and document: parse latency for a large SV file, `definition` query
latency, memory footprint. Add `make benchmark` target.

---

## 6.6 Packaging

**Status:** Not started

`make install` CMake target + `docs/usage.md` end-user guide.
