# svlsp — Handoff Document

**Date:** 2026-07-09  
**Last completed phase:** Phase 6.3 complete (6.1 — DB-backed providers; 6.3 — package import
*and export* resolution; preprocessor source map committed)  
**Current work:** Phase 6.2 (multi-file project support) IN PROGRESS — Stage 3 of 6 complete
and committed. **Full approved plan, with all 6 stages spelled out in file-level
detail (exact signatures, schema SQL, algorithms, test names), lives at
`/home/martin/.claude/plans/fluffy-hatching-popcorn.md` — read that file first before resuming.**
See "Phase 6.2" section below for a summary and current status of each stage.

---

## What this project is

A SystemVerilog Language Server Protocol (LSP) server written in C++20.
The full scope is documented in `plan.md`. Short version:

- **LSP protocol layer** (C++, lsp-framework) — talks to Emacs/editors
- **ANTLR4 compiler front-end** — parses SystemVerilog into an AST
- **SQLite database** — stores compilation artefacts for incremental builds and LSP queries
- **Emacs daemon + lsp-mode** — automated end-to-end functional test client

---

## Repository layout

```
src/lsp/           server_state, server, document_store, diagnostics,
                   hover, definition, references, completion,
                   document_symbols, workspace_symbols, rename,
                   signature_help, symbol_utils — LSP layer (Phase 6.1 complete)
src/compiler/      compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                   parse_record, parse_cache — compiler front-end (Phase 4 complete)
src/db/            database, symbol_database, compilation_controller,
                   schema — SQLite persistence layer (Phase 5 complete, schema v3)
src/main.cpp       entry point
tests/unit/        Catch2 unit tests (268 cases, 672 assertions)
tests/integration/ Emacs functional test scripts (19 files, 84 test cases)
tools/             emacs-test-daemon.sh, emacs-test-init.el, emacs-test-lib.sh
examples/          20 .sv fixture files (all created in Phase 4.2)
grammar/           Sv.g4 — 3828-line SystemVerilog grammar (Phase 4.1 complete)
cmake/             CMake helper modules
  ANTLR4Tool.cmake  tool detection: PATH → antlr4 cmd; fallback → download JAR
build/debug/generated/antlr4/   generated SvLexer/SvParser/SvVisitor sources (not committed)
docs/              per-phase docs and architecture decision records
plan.md            full phased plan — read this first
```

---

## Build and test

```bash
# Build
cmake --preset debug
cmake --build --preset debug
# or:
make configure build

# Unit tests
make test-unit
# or: build/debug/unit_tests

# Parser tests only
build/debug/unit_tests "[compiler][parser]"

# Integration tests (Emacs daemon, requires display or Xvfb)
DISPLAY=:99 make test-integration   # runs all 17 test files
# or individually:
bash tools/emacs-test-daemon.sh tests/integration/test_05_hover.sh

# Smoke-test the binary directly
printf 'Content-Length: 152\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}' | build/debug/svlsp
```

Compiler: **g++-13** (system g++ 7.5 does not support C++20 — pinned in `CMakePresets.json`).  
ASan + UBSan are enabled in debug builds.

---

## Architecture of what exists

### State machine (`src/lsp/server_state.h/.cpp`)

Pure business logic, no I/O. Directly instantiable in unit tests.

```
Uninitialized ──(initialize)──► Active ──(shutdown)──► Shutdown ──(exit)──► Inactive
```

- Wrong-state requests throw `lsp::RequestError` with the correct LSP error code.
- `initialized` notification is a no-op (reserved for cache warm-up).
- `exit` without prior `shutdown` is allowed by spec; both paths set `Inactive`.

Advertised capabilities (all Phase 3 providers registered):

```cpp
positionEncoding     = UTF16
textDocumentSync     = Full (openClose + save enabled)
completionProvider   = CompletionOptions{}
hoverProvider        = true
signatureHelpProvider= SignatureHelpOptions{}
definitionProvider   = true
referencesProvider   = true
documentSymbolProvider  = true
workspaceSymbolProvider = true
renameProvider       = true
serverInfo           = { name: "svlsp", version: "0.1.0" }
```

**Important:** C++ designated initialisers must follow `ServerCapabilities` declaration
order. `completionProvider` and `signatureHelpProvider` use `Opt<XxxOptions>` (not
`OneOf<bool, XxxOptions>`). `signatureHelpProvider` sits between `hoverProvider` and
`definitionProvider` in the struct — insert it there, not at the end.

### Document store (`src/lsp/document_store.h/.cpp`)

Pure business logic, no I/O. Tracks open documents as `uri → { text, version }`.

| Method | Description |
|---|---|
| `open(DidOpenTextDocumentParams)` | Stores URI → { text, version } |
| `update(DidChangeTextDocumentParams)` | Replaces text and version (Full sync) |
| `close(DidCloseTextDocumentParams)` | Removes the document |
| `contains(uri)` | Returns true if the URI is currently open |
| `get(uri)` | Returns the stored document; throws `std::out_of_range` if unknown |

### Diagnostics publisher (`src/lsp/diagnostics.h/.cpp`)

Sends `textDocument/publishDiagnostics` notifications to the client.

| Method | Description |
|---|---|
| `publish(uri, version, diags={})` | Sends `publishDiagnostics` via `MessageHandler` |
| `static buildParams(uri, version, diags={})` | Builds params without sending — for unit tests |

### Symbol utilities (`src/lsp/symbol_utils.h/.cpp`)

Shared helpers used by all LSP feature providers:

| Function | Description |
|---|---|
| `symbolKindFor(kind)` | Maps DB kind string → `lsp::SymbolKind` |
| `completionKindFor(kind)` | Maps DB kind string → `lsp::CompletionItemKind` |
| `wordAtPosition(text, line, char)` | Extracts identifier at 0-based cursor position; walks left even when cursor is on a non-id character (intentional — completion needs the prefix to the left of the insertion point) |
| `makeRange(line1, col0, nameLen)` | Converts 1-based line to 0-based `lsp::Range` |
| `pathToUri(path)` | Calls `lsp::FileUri::fromPath(path)` |

### LSP feature providers (Phase 6.1 — DB-backed)

Each lives in `src/lsp/<name>.h/.cpp`. All five providers now accept a
`SymbolDatabase&` and return real results from the SQLite database.

| File | Class | Signature | Behaviour |
|---|---|---|---|
| `hover.h/.cpp` | `HoverProvider` | `getHover(HoverParams, SymbolDatabase&, docText)` | `wordAtPosition` → `findSymbolsByName` → Markdown `**Kind** \`name\`` |
| `definition.h/.cpp` | `DefinitionProvider` | `getDefinition(DefinitionParams, SymbolDatabase&, docText)` | `wordAtPosition` → `findSymbolsByName` → `Location` |
| `completion.h/.cpp` | `CompletionProvider` | `getCompletion(CompletionParams, SymbolDatabase&, docText)` | `findSymbolsVisibleAt(path, line1)` → filter by prefix → `CompletionItem[]` |
| `document_symbols.h/.cpp` | `DocumentSymbolsProvider` | `getDocumentSymbols(DocumentSymbolParams, SymbolDatabase&)` | `symbolsForFile` → `DocumentSymbol[]` with scope ranges |
| `workspace_symbols.h/.cpp` | `WorkspaceSymbolsProvider` | `getWorkspaceSymbols(WorkspaceSymbolParams, SymbolDatabase&)` | `findSymbolsByNamePrefix(query)` → `WorkspaceSymbol[]` |
| `references.h/.cpp` | `ReferencesProvider` | `getReferences(ReferenceParams)` | Returns `nullptr` — Phase 6.2+ |
| `rename.h/.cpp` | `RenameProvider` | `getRename(RenameParams)` | Returns `nullptr` — Phase 6.2+ |
| `signature_help.h/.cpp` | `SignatureHelpProvider` | `getSignatureHelp(SignatureHelpParams)` | Returns `nullptr` — Phase 6.2+ |

Position-based providers (hover, definition, completion) check `m_store.contains(uri)`
before querying the DB and return `nullptr` if the document is not open.

### I/O wrapper (`src/lsp/server.h/.cpp`)

`LanguageServer` owns (in construction order):
`m_db`, `m_symbolDb`, `m_compiler`, `m_connection`, `m_messageHandler`, `m_store`, `m_diagnostics`.

`m_db` is opened as `":memory:"` — symbols persist across requests within one server session
but are lost on restart. The DB is re-populated on every `didOpen`/`didChange` via
`m_compiler.compile(path, text)`.

`registerHandlers()` wires all message types:

| Message | Kind | Handler |
|---|---|---|
| `initialize` | request | `handleInitialize` — stores processId, returns capabilities |
| `initialized` | notification | `handleInitialized` — no-op |
| `shutdown` | request | `handleShutdown` — transitions to Shutdown |
| `exit` | notification | `handleExit` — transitions to Inactive, breaks run() loop |
| `textDocument/didOpen` | notification | `m_store.open()` → `m_compiler.compile()` → `m_diagnostics.publish()` |
| `textDocument/didChange` | notification | `m_store.update()` → `m_compiler.compile()` → `m_diagnostics.publish()` |
| `textDocument/didClose` | notification | `m_store.close()` |
| `textDocument/hover` | request | `HoverProvider::getHover(params, m_symbolDb, docText)` |
| `textDocument/definition` | request | `DefinitionProvider::getDefinition(params, m_symbolDb, docText)` |
| `textDocument/references` | request | `ReferencesProvider::getReferences(params)` — null |
| `textDocument/completion` | request | `CompletionProvider::getCompletion(params, m_symbolDb, docText)` |
| `textDocument/documentSymbol` | request | `DocumentSymbolsProvider::getDocumentSymbols(params, m_symbolDb)` |
| `workspace/symbol` | request | `WorkspaceSymbolsProvider::getWorkspaceSymbols(params, m_symbolDb)` |
| `textDocument/rename` | request | `RenameProvider::getRename(params)` — null |
| `textDocument/signatureHelp` | request | `SignatureHelpProvider::getSignatureHelp(params)` — null |

---

## Emacs test infrastructure

`tools/emacs-test-daemon.sh` drives all functional tests:

1. First run: installs lsp-mode from MELPA into `.emacs-test/` (stamp file prevents re-download).
2. Starts `emacs --daemon=svlsp-test-<PID>` with `tools/emacs-test-init.el`.
3. Sources `tools/emacs-test-lib.sh`, then runs each `test_*.sh` file passed as arguments.
4. Reports pass/fail counts; kills daemon on exit.

`tools/emacs-test-init.el` registers svlsp as the LSP server for `verilog-mode`.

Helper functions: `svlsp-test/open-file`, `svlsp-test/wait-for-lsp`, `svlsp-test/close-file`.

### Integration test patterns

- All Elisp passed to `emacsclient` must be wrapped in `(condition-case err ... (error ...))`.
  The daemon script uses `set -euo pipefail`; an uncaught Elisp error returns exit 1 and kills the script.
- `(lsp-workspaces)` is buffer-local — always call inside `(with-current-buffer buf ...)`.
- `(lsp--get-buffer-diagnostics)` reads per-buffer diagnostics.
- `lsp-request` returns `nil` for a JSON null response — use `(null result)` to check.
- `workspace/symbol` requests can be sent from any buffer context; the query spans all indexed files.

---

## Key decisions

| Decision | Choice | Where documented |
|---|---|---|
| LSP framework | lsp-framework v1.3.1 (submodule) | `docs/decisions/lsp-framework.md` |
| Unit test framework | Catch2 v3.8.1 (FetchContent) | `CMakeLists.txt` |
| Transport | stdio (stdin/stdout) | `src/main.cpp` |
| Compiler | g++-13 | `CMakePresets.json` |
| Parser generator | ANTLR4 v4.13.2 (FetchContent) | `CMakeLists.txt` |
| Database | SQLite3 (amalgamation, schema v3) | `src/db/schema.h` |
| SV directive taxonomy | Two-pass: strip compiler directives first, preprocess second | `docs/decisions/sv-preprocessor.md` (complete) |
| `__FILE__` / `__LINE__` | Resolved in pass 1 against original source, before include shifts line numbers | `plan.md §4.2b` |
| SV preprocessor tool | Minimal in-house C++ — slang upgrade path documented | `docs/decisions/sv-preprocessor.md` (complete) |
| SQLite ORDER BY after UNION ALL | Expressions like `length(scope)` are not allowed; sort in C++ | `src/db/symbol_database.cpp findSymbolsVisibleAt` |

---

## Working rules (from plan.md)

- Every function has a **unit test before the implementation is written**.
- Every LSP feature needs **both** a unit test and a functional Emacs test (real JSON-RPC over wire). Neither alone is sufficient.
- Two commits per feature: `feat(<module>): ...` then `docs(<module>): ...`.
- `main` branch is always green (unit + functional tests passing).

---

## Phase 3 status — Complete

| Sub-phase | Feature | LSP method | Status |
|---|---|---|---|
| 3.1 | Text document sync | `didOpen`, `didChange`, `didClose` | Complete |
| 3.2 | Diagnostics | `textDocument/publishDiagnostics` | Complete |
| 3.3 | Hover | `textDocument/hover` | Complete (real results — Phase 6.1) |
| 3.4 | Go-to-definition | `textDocument/definition` | Complete (real results — Phase 6.1) |
| 3.5 | Find references | `textDocument/references` | Wired (null — Phase 6.2+) |
| 3.6 | Completion | `textDocument/completion` | Complete (real results — Phase 6.1) |
| 3.7 | Document symbols | `textDocument/documentSymbol` | Complete (real results — Phase 6.1) |
| 3.8 | Workspace symbols | `workspace/symbol` | Complete (real results — Phase 6.1) |
| 3.9 | Rename | `textDocument/rename` | Wired (null — Phase 6.2+) |
| 3.10 | Signature help | `textDocument/signatureHelp` | Wired (null — Phase 6.2+) |

---

## Phase 4 status — Complete

### 4.1 Grammar Integration — Complete

| Step | Status | Notes |
|---|---|---|
| ANTLR4 tool detection in CMake | **Done** | `cmake/ANTLR4Tool.cmake` — PATH first, JAR fallback |
| Fetch `Sv.g4` → `grammar/Sv.g4` | **Done** | 3828 lines from `miguel-guerrero/antlr4_system_verilog_parser` |
| ANTLR4 C++ runtime (FetchContent) | **Done** | `antlr/antlr4` GIT_SHALLOW + `SOURCE_SUBDIR runtime/Cpp`, v4.13.2 |
| CMake `add_custom_command` for generation | **Done** | Generates SvLexer/SvParser/SvListener/SvVisitor/SvBase* |
| `svlsp_antlr4` static lib target | **Done** | Strict warnings suppressed (`-w`) on machine-generated code |
| Unit tests: parse fixtures + error detection | **Done** | 4 tests in `tests/unit/compiler/test_sv_parser.cpp` |

#### Architecture note — generated targets

```
grammar/Sv.g4
    └─(add_custom_command: antlr4 -Dlanguage=Cpp -visitor)
        └─ build/debug/generated/antlr4/
               SvLexer.{h,cpp}  SvParser.{h,cpp}
               SvListener.{h,cpp}  SvBaseListener.{h,cpp}
               SvVisitor.{h,cpp}   SvBaseVisitor.{h,cpp}
               └─ svlsp_antlr4 (static lib, links antlr4_static)
                      └─ unit_tests (links svlsp_antlr4 directly)
                         (svlsp_lib will link svlsp_antlr4 in Phase 4.5)
```

### 4.2 SystemVerilog Example Library — Complete

All 20 fixture files exist in `examples/`. All 22 parser test cases pass.

**Grammar quirks discovered during Phase 4.2** (see section below).

### 4.2a Directive Taxonomy and Scope — Complete

Two-pass pipeline:
- **Pass 1 — compiler directive strip (§4.2b):** metadata directives, `__FILE__`, `__LINE__`
- **Pass 2 — preprocessor (§4.2c):** `` `define ``, `` `ifdef ``, `` `include ``, macro invocations

### 4.2b Compiler Directive Strip Pass — Complete

`src/compiler/compiler_directive_stripper.h/.cpp`, `tests/unit/compiler/test_compiler_directive_stripper.cpp`

### 4.2c Preprocessor Tool Selection and Integration — Complete

`src/compiler/sv_preprocessor.h/.cpp`, `tests/unit/compiler/test_sv_preprocessor.cpp`

### 4.3 AST Visitor / Listener — Complete

`SvTreeWalker` in `src/compiler/sv_tree_walker.h/.cpp`.

Scope stack tracks Module/Interface/Package/Class/Function/Task/**Program** — names are
pushed on enter hooks and popped on exit hooks. Each `ParseRecord` carries the `scope` chain
(e.g. `"MyModule::MyClass"`) and `endLine` (last line of scope body, for range building).
Exit hooks call `backpatchEndLine` to patch the record after the closing token is seen.
(`Program` support added in Phase 6.2 Stage 1 — previously programs weren't tracked at all.)

### 4.4 Symbol Extraction — Complete

`ParseRecord` fields: `kind, name, line, column, parent, detail, endLine, scope`.

- `endLine` — 1-based last line of scope body; 0 for leaf symbols (ports, signals, parameters)
- `scope` — full enclosing scope chain (e.g. `"MyModule::MyClass"`); `""` for top-level symbols

**`InstantiationRecord`** (Phase 6.2 Stage 1, `src/compiler/parse_record.h`): one per
module/interface/program instantiation (`Foo u0(...);`), fields `typeName, instName,
line, file`. Emitted by `enterModule_instantiation`/`enterInterface_instantiation`/
`enterProgram_instantiation` in `sv_tree_walker.cpp`; `WalkResult` carries it as
`instantiations`. This is a *reference*, not a declaration — it's how the Phase 6.2
library resolver (`-y`/`-v` filelist support, not yet implemented) will know which
instantiated type names aren't declared anywhere yet.

### 4.5 Error Recovery — Complete

`ParseError { line, column, message }`. `SvErrorListener` installed on lexer + parser.
`DiagnosticsPublisher::buildDiagnostic(ParseError)` converts to `lsp::Diagnostic`.

### 4.6 Incremental Parsing — Complete

`ParseCache` replaced by `CompilationController` (Phase 5.4) which uses SQLite hash storage.

---

## Phase 5 — SQLite Database Layer — Complete

### Schema v5

Defined in `src/db/schema.h`. `db::SCHEMA_VERSION = 5`.

Five tables:
- `files (id, path UNIQUE, content_hash, parsed_at)`
- `symbols (id, file_id→files, kind, name, line, col, parent, detail, end_line, scope)` —
  indexes on `name`, `file_id`, `scope`, `(scope,name)`, `(file_id,line,end_line)`
- `diagnostics (id, file_id→files, line, col, message)`
- `imports (id, file_id→files, pkg_name, item, is_export)` — `item = "*"` for wildcard
  imports; `is_export = 1` for `export pkg::item`/`export pkg::*` declarations;
  index on `file_id`
- `instantiations (id, file_id→files, type_name, inst_name, line)` — one row per
  module/interface/program instantiation (`Foo u0(...)`); indexes on `file_id`
  and `type_name`. Drives `unresolvedInstantiatedTypeNames()` (Phase 6.2's
  library resolver — see that section above).

Migrations run automatically in `database.cpp`:
- `MIGRATION_V1_TO_V2`: adds `end_line` and `scope` columns plus three new indexes
- `MIGRATION_V2_TO_V3`: adds the `imports` table and its index
- `MIGRATION_V3_TO_V4`: adds the `is_export` column to `imports` (default 0)
- `MIGRATION_V4_TO_V5`: adds the `instantiations` table and its two indexes

### 5.2 Database Abstraction Layer — Complete

`src/db/database.h/.cpp` — RAII `Database` wrapper around `sqlite3*`.

### 5.3 Query API — Complete

`src/db/symbol_database.h/.cpp`:

| Method | Description |
|---|---|
| `upsertFile(path, hash) → file_id` | INSERT or UPDATE; stable id for same path |
| `getFileHash(path) → string` | Returns `""` for unknown paths |
| `replaceSymbols(file_id, records)` | DELETE + INSERT in a transaction (9 columns incl. end_line, scope) |
| `replaceDiagnostics(file_id, errors)` | DELETE + INSERT in a transaction |
| `symbolsForFile(path) → vector<SymbolRow>` | Ordered by line |
| `findSymbolsByName(name) → vector<SymbolRow>` | Cross-file, with path |
| `diagnosticsForFile(path) → vector<DiagnosticRow>` | |
| `findSymbolsInScope(scope) → vector<SymbolRow>` | All symbols with exactly this scope value |
| `findSymbolsByNamePrefix(prefix) → vector<SymbolRow>` | LIKE `prefix%`, cross-file |
| `scopeAtPosition(path, line) → string` | Innermost scope-defining symbol containing `line` (1-based); returns `""` if top-level |
| `findSymbolsVisibleAt(path, line) → vector<SymbolRow>` | UNION ALL: (1) file-local scope chain, (2) cross-file top-level + wildcard-imported package scopes (plus their transitively re-exported packages), (3) one arm per specific import (plus re-exported specific items); C++ sorted by scope depth then name |
| `replaceImports(file_id, imports)` | DELETE + INSERT import records (incl. `is_export`) in a transaction |
| `importsForFileId(file_id) → vector<ImportRow>` | Returns `{ pkgName, item, isExport }` for the given file |
| `fileIdForPackage(pkgName) → int64_t` *(private)* | file_id of the file declaring top-level package `pkgName`, or -1 |
| `collectExportedImports(pkgName, ...)` *(private)* | Recursively follows `export pkg::*`/`export pkg::item` reachable from `pkgName`; cycle-safe via a `visited` list |
| `replaceInstantiations(file_id, insts)` | DELETE + INSERT instantiation records in a transaction |
| `unresolvedInstantiatedTypeNames() → vector<string>` | Distinct `type_name`s instantiated somewhere with no matching Module/Interface/Program declaration anywhere in the DB — drives Phase 6.2's library resolver |
| `appendDiagnostics(file_id, extra)` | INSERT-only (unlike `replaceDiagnostics`'s delete-then-insert) — lets the library resolver attach diagnostics without wiping a file's own ANTLR diagnostics |

`SymbolRow { id, kind, name, line, col, parent, detail, filePath, endLine, scope }`.

**SQLite UNION ALL ORDER BY limitation:** expressions like `length(scope)` are not
allowed in `ORDER BY` after a compound SELECT — only bare output column names are valid.
`findSymbolsVisibleAt` therefore omits the `ORDER BY` clause and sorts with `std::sort`
in C++ after fetching all rows.

### 5.4 Incremental Compilation Controller — Complete

`src/db/compilation_controller.h/.cpp` — hash check → skip or recompile → update DB.

### Library dependency graph

```
svlsp_compiler  (compiler_directive_stripper, sv_preprocessor, sv_tree_walker, parse_cache)
    → svlsp_antlr4

svlsp_db  (database, symbol_database, compilation_controller)
    → svlsp_sqlite3
    → svlsp_compiler

svlsp_lib  (lsp/*, no compiler sources)
    → lsp
    → svlsp_compiler
    → svlsp_db
```

---

## Preprocessor source map — Complete

`SvPreprocessor::process` returns a `sourceMap: vector<SourceLine>` alongside the
expanded text. Each entry maps one output line (index = line − 1) back to its
original `{file, line}`:

- `SourceLine.file == ""` → line belongs to the primary compiled file
- `SourceLine.file == "/path/to/inc.sv"` → line belongs to that included file

`SvTreeWalker::walk` consumes the map via `translateLine`, converting every
`ParseRecord` and `ParseError` to original-file coordinates before they leave the
compiler layer. `CompilationController::compile` groups by `file` and calls
`replaceSymbols`/`replaceDiagnostics` per distinct file, routing included-file
records to their own `file_id`.

Integration tests 15 (`test_15_preprocessor_lsp.sh`, 9 tests) verify that
hover, definition, completion, documentSymbol, and workspaceSymbol all report
correct paths and line numbers through the source map.

---

## Phase 6.3 — Package Import Resolution — Complete

`import pkg::*` (wildcard) and `import pkg::Foo` (specific) imports are now tracked
and used to extend `findSymbolsVisibleAt`.

### New components

**`ImportRecord`** (`src/compiler/parse_record.h`):
```cpp
struct ImportRecord {
    std::string pkgName;   // package being imported/exported
    std::string item;      // symbol name, or "*" for wildcard
    int         line{0};
    std::string file{};    // empty = primary compiled file
    bool        isExport{false}; // true for `export`, false for plain `import`
};
```

**ANTLR4 hook** (`SvRecordListener::enterPackage_import_item`):
Fires on every `import pkg::item` **and** `export pkg::item` statement (both
alternatives reuse the same `package_import_item` grammar production).
`enterPackage_export_declaration`/`exitPackage_export_declaration` toggle an
`m_inExport` flag around the export form so the emitted `ImportRecord` can be
stamped `isExport = true`. Translates the token line via the source map and
pushes onto `m_imports`. `WalkResult` gains a third field: `imports`.

**`imports` DB table** (schema v4):
`(id, file_id, pkg_name, item, is_export)` — `item = "*"` for wildcards,
`is_export = 1` for `export` declarations. `replaceImports` is called by
`CompilationController::compile` alongside `replaceSymbols`.
`MIGRATION_V3_TO_V4` adds the `is_export` column (default 0) to existing DBs.

**Extended `findSymbolsVisibleAt`** (`src/db/symbol_database.cpp`):
Loads the file's import records, then builds a three-part UNION ALL query:
1. File-local symbols in the scope chain (unchanged)
2. Cross-file symbols where `scope IN ('', ...wildcardPkgs)` — adds each
   wildcard-imported package scope to the permitted set
3. One additional UNION ALL arm per specific import: `scope = pkg AND name = item`

**Export re-exports** (`SymbolDatabase::collectExportedImports`, cycle-safe via
a `visited` list): for each directly wildcard-imported package, looks up that
package's own declaring file (`fileIdForPackage`) and follows its `export
pkg::*` / `export pkg::item` declarations, merging re-exported wildcard
packages and specific items into the same `wildcardPkgs`/`specificImports`
sets used above. **Plain (non-exported) imports are never followed** — only
the immediately imported scope is visible unless that scope explicitly
re-exports it. Only the `export pkg::item` / `export pkg::*` grammar
alternative is handled; the LRM's `export *::*;` shorthand (re-export
everything imported into the current scope, regardless of package) is not
wired up — that literal doesn't route through `package_import_item` at all.

### Tests

Unit: `[import]`/`[export]` test cases across `test_sv_listener.cpp` (export
vs. plain-import tagging) and `test_symbol_database.cpp` (transitive-export
resolution, plus a regression test proving a plain import does *not* leak a
second-level import).

Integration (`test_17_import_resolution.sh`, 10 tests):
- Prerequisite: open `util_pkg.sv` to seed DB
- Wildcard: completion includes all three util_pkg symbols (DataItem, Logger, compute)
- Wildcard: hover on DataItem → non-null; definition → util_pkg.sv at LSP line 2
- Specific: completion includes DataItem; excludes Logger and compute

Fixtures: `tests/integration/fixtures/{util_pkg,import_wildcard,import_specific}.sv`

Integration (`test_18_export_resolution.sh`, 10 tests):
- Prerequisites: seed `base_pkg.sv`, `middle_pkg.sv` (imports + exports
  `base_pkg::*`), and `plain_middle_pkg.sv` (imports `base_pkg::*`, no export)
- Export: completion in `export_user.sv` (imports only `middle_pkg::*`)
  includes both `Beta` (middle_pkg's own) and `Alpha` (re-exported from
  `base_pkg`); hover/definition on `Alpha` confirm it resolves to
  `base_pkg.sv` at its true declaration line — not merely that a same-named
  symbol is visible
- Regression: completion in `plain_import_user.sv` (imports only
  `plain_middle_pkg::*`, which does *not* export) includes `Gamma` but
  excludes `Alpha` — proving plain imports still don't leak transitively

Fixtures: `tests/integration/fixtures/{base_pkg,middle_pkg,export_user,plain_middle_pkg,plain_import_user}.sv`

---

## Phase 6.2 — Multi-File Project Support — IN PROGRESS (Stage 3/6 complete)

**Full plan file (read this first to resume):**
`/home/martin/.claude/plans/fluffy-hatching-popcorn.md` — contains the complete
approved design: exact struct/method signatures, schema SQL, the library-resolution
fixpoint algorithm spelled out step-by-step, file/test naming, and PR sequencing.
This section is a status summary only; the plan file is the source of truth.

### Scope (confirmed with the user)

- Support **two** independent project-config formats, both producing one shared
  `ProjectConfig` struct: a custom, extensible `.svlsp.json` manifest, **and** a
  VCS/Questa/Xcelium-style `.f` filelist (for interop with existing EDA build flows).
- The filelist parser implements **full `-y`/`-v`/`+libext+` library resolution**
  (auto-discover a module's defining file by name when referenced/instantiated
  but not explicitly listed) — not a stub.
- Any `.f` switch not explicitly supported is a **hard error**, not silently ignored.
- Unresolved instantiations (not found in project files, `-v` files, or `-y` dirs)
  **emit a diagnostic** on the referencing file, reusing the existing `ParseError`
  pipeline (confirmed with the user — see plan file §Stage 4).

### Stage status

| Stage | What | Status |
|---|---|---|
| 1 | Program tracking (`ParseRecordKind::Program`) + `InstantiationRecord` + schema v5 (`instantiations` table) + `unresolvedInstantiatedTypeNames`/`appendDiagnostics` | **Complete** — commit `fe0f817`, 15 new unit tests, full suite 268 cases/672 assertions passing |
| 2 | `.f` filelist parser (`src/compiler/filelist_parser.h/.cpp`, `ProjectConfig` in `src/compiler/project_config.h`) | **Complete** — 13 new unit tests, full suite 281 cases/702 assertions passing |
| 3 | `.svlsp.json` manifest parser (`src/lsp/project_manifest_parser.h/.cpp`, via `lsp::json`) | **Complete** — 10 new unit tests, full suite 291 cases/738 assertions passing |
| 4 | Thread `ProjectConfig` into `CompilationController::compile`; `LibraryResolver` (-v/-y fixpoint); `ProjectCompiler` batch loader | Not started |
| 5 | Server wiring: capture `rootUri`/`initializationOptions` in `ServerState`; new `ProjectRegistry` (upward-search discovery, caching, lazy load) | Not started |
| 6 | End-to-end Emacs test `test_21_multifile_project.sh` + `multifile_project/` fixtures (renumbered from `test_19` after two unrelated macro-expansion regression tests were inserted — see "Known gaps") | Not started |

### Key facts discovered during planning (still true, don't re-derive)

- `program` declarations were **not tracked at all** before Stage 1 — now fixed
  (mirrors Module/Interface hooks exactly; grammar rules confirmed at
  `grammar/Sv.g4:89-101,3673`).
- **Hover/Definition already do global cross-file lookup** via `findSymbolsByName`
  (no scope/file filtering) — so once a library-resolved file's symbols land in
  the DB, hover/definition on an instantiation site work with **zero changes**
  to `hover.cpp`/`definition.cpp`. The only new capability needed is the
  *resolver* knowing which names to search for.
- No JSON library is linked except lsp-framework's own `lsp::json` (confirmed
  API: `isObject()`/`object()`/`find()`/`isString()`/`string()`) — already
  transitively available via `svlsp_lib`, but **not** via `svlsp_compiler`
  (confirmed: `svlsp_compiler` links only `svlsp_antlr4`/`svlsp_options`). This
  is why the JSON manifest parser must live under `src/lsp/`, while the filelist
  parser belongs in `src/compiler/`.
- `InitializeParams` (generated `types.h:6080-6159`) has `rootUri`
  (`NullOr<DocumentUri>`), `rootPath` (`Opt<NullOr<String>>`),
  `initializationOptions` (`Opt<LSPAny>`, `LSPAny = json::Value`), and
  `workspaceFolders` — all currently unread anywhere in the codebase.
- Filelist format confirmed via web research (VCS/Questa/Xcelium): bare
  filenames; `+define+NAME[=VALUE]` and `+incdir+DIR` chainable on `+`;
  `-f FILE` (nested, CWD-relative) vs `-F FILE` (nested, relative to the
  filelist's own dir); `-sv`/`-sverilog`; `-y DIR`; `-v FILE`; `+libext+.ext`
  chainable; `-top MODULE`; `//` comments; double-quoted filenames.

### Stage 2 — `.f` filelist parser — Complete

`src/compiler/project_config.h` (new, header-only `ProjectConfig`/`SvLanguageMode`)
and `src/compiler/filelist_parser.h/.cpp` (new, `FilelistParser::parse(path)`),
both registered in `CMakeLists.txt` under `svlsp_compiler`.

- **CWD-relative vs. file-relative resolution is implemented via a per-recursion-frame
  `baseDir` string**, not a global. `-f FILE`: recurses with the *same* `baseDir` as the
  current frame (paths inside the nested file stay CWD-relative, matching vendor tool
  behavior). `-F FILE`: recurses with `baseDir` = the nested file's own parent directory.
  The top-level `parse(path)` call seeds `baseDir = fs::current_path()` — i.e. the entry
  point behaves as if it were itself `-f`'d in from the CWD.
- **Cycle detection uses an "active recursion stack" set** (`insert` on entry,
  `erase` on return), not a permanent "ever visited" set — so a diamond include
  (A includes B and C; both B and C include D) is legal and D is parsed twice
  (harmless: `compile()` is content-hash-cached downstream), while true cycles
  (A → B → A) throw. Don't switch this to a permanent-visited set without checking
  this distinction is still wanted.
- Any `-x`/`+x` token not in the explicitly supported list throws
  `std::runtime_error` naming the offending token and `path:line` — no silent
  ignoring, per the confirmed scope above.
- Unit tests: `tests/unit/compiler/test_filelist_parser.cpp`, tag `[compiler][filelist]`
  — 13 cases covering every bullet in the Context section's format list, using real
  temp files under `/tmp/svlsp_test_filelist/` (nested `-f`/`-F` targets must exist on
  disk since the parser opens them to canonicalize for cycle detection).

### Stage 3 — `.svlsp.json` manifest parser — Complete

`src/lsp/project_manifest_parser.h/.cpp` (new, `ProjectManifestParser::parse(path)`),
registered in `CMakeLists.txt` under `svlsp_lib` (not `svlsp_compiler` — needs `lsp::json`,
confirmed unavailable there; see "Key facts" above).

- `lsp::json::parse`'s `ParseError` and `Value::string()`/`object()`'s `TypeError`
  both derive from `lsp::Exception → std::runtime_error`, so they already satisfy
  "throws `std::runtime_error`" — caught once at the top of `parse()` and rewrapped
  with the manifest path prepended for a clearer message; every other validation
  (wrong-typed field, non-object root, invalid `"mode"` value) throws its own
  `std::runtime_error` directly, naming the offending field.
- **Unknown top-level keys are silently ignored** (JSON is self-describing — a
  typo'd key can't corrupt parsing of an unrelated field), the deliberate opposite
  of the filelist parser's hard-error policy — see the plan file's design note if
  you want to revisit that asymmetry.
- `"mode"` only accepts the literal strings `"sv"` / `"v95"`; anything else throws
  (not in the original plan spec, added defensively since an unrecognized mode
  string silently keeping the default would be a worse failure mode than an error).
- Relative paths in `"files"`/`"includeDirs"`/`"libraryDirs"`/`"libraryFiles"`
  resolve against the manifest's own parent directory (`libExtensions` values are
  bare extension strings, not paths — no resolution).
- Unit tests: `tests/unit/lsp/test_project_manifest_parser.cpp`, tag
  `[lsp][project-manifest]` — 10 cases: full/partial fields, `"mode":"v95"`,
  unknown key ignored, malformed JSON, non-object root, wrong-typed `"files"` and
  `"defines"` values, invalid `"mode"` value, missing file on disk.

---

## Phase 6.1 — DB-Backed LSP Providers — Complete

All five active providers rewritten to query `SymbolDatabase`:

- **DocumentSymbols** (`symbolsForFile`) — builds `DocumentSymbol[]`; scope symbols get a
  multi-line `range` (`endLine`-based) and a point `selectionRange` at the identifier.
  Leaf symbols (endLine = 0) get `range == selectionRange`.
- **WorkspaceSymbols** (`findSymbolsByNamePrefix`) — builds `WorkspaceSymbol[]` with `Location`.
- **Hover** (`wordAtPosition` + `findSymbolsByName`) — prefers same-file match; returns
  Markdown `**Kind** \`name\`` with optional detail and scope.
- **Definition** (`wordAtPosition` + `findSymbolsByName`) — returns first matching `Location`.
- **Completion** (`findSymbolsVisibleAt`) — scope-aware; filters by any already-typed prefix;
  returns `CompletionItem[]` with `completionKindFor` and optional `detail`.

Integration tests 05/06/08/09/10 updated from "expect null" to verify real results.

### Phase 6 sub-phase status

| Sub-phase | Feature | Status |
|---|---|---|
| 6.1 | DB-backed LSP providers | **Complete** |
| 6.2 | Multi-file project support (`.svlsp.json` + `.f` filelist, incl. `-y`/`-v` library resolution) | **In progress — Stage 3/6 complete**, see Phase 6.2 section above |
| 6.3 | Package import/export resolution (`import pkg::*`, `export pkg::*`) | **Complete** |
| 6.4 | Cross-file invalidation (dependency graph) | Not started — see `plan.md §6.4` |
| 6.5 | Performance baseline | Not started |
| 6.6 | Packaging / `make install` | Not started |

---

## Sv.g4 grammar quirks (discovered in Phase 4.2)

| Construct | Expected SV | Grammar behaviour | Workaround |
|---|---|---|---|
| Backtick directives | `` `define ``, `` `ifdef ``, `` `timescale `` | Not in grammar at all — no lexer rules | Must be preprocessed before parsing (Phase 4.2a) |
| `bind` double semicolon | `bind M C u (.p(p));` | `bind_directive` adds `';'` on top of `module_instantiation`'s own `';'` | Write `bind M C u (.p(p));;` |
| `bind` parameter override | `bind M C #(.W(W)) u (.p(p));;` | LL(\*) prediction fails after `#(...)` | Omit parameter override; use default params |
| Void cast | `void'(f())` | `void SINGLE_QUOTE '('` not in grammar | Use `void(f())` form (grammar line 2421) |
| Cross body `ignore_bins` | `cross A, B { ignore_bins x = ...; }` | `cross_body_item` already consumes `';'`, then `cross_body` adds another — double semicolon | Use `cross A, B;` (empty cross body) |
| `timeunit`/`timeprecision` vs `` `timescale `` | `` `timescale 1ns/1ps `` | No backtick directive support | Use `timeunit 1ns; timeprecision 1ps;` inside module |

---

## Known gaps / things to watch out for

- `handleExit` does not distinguish clean vs. abnormal exit for the process exit code.
  `LanguageServer::run()` always returns 0. Fix when exit-code handling is needed.
- `m_parentProcessId` is stored but never used — reserved for parent-process monitoring.
- The Emacs test harness requires a graphical display (or Xvfb) because lsp-mode starts a
  child process and monitors its output. Run `Xvfb :99 &; DISPLAY=:99 make test-integration`
  in a headless environment.
- lsp-framework's `messages.h` is generated at build time by `lspgen`. A clean build takes
  longer than a rebuild. This is normal.
- `CompilationController` uses `":memory:"` SQLite — symbols are lost on server restart.
  Each file must be re-opened for its symbols to reappear. Cross-session persistence
  requires a file-backed DB path (straightforward swap, just change the path in `server.cpp`).
- The `export *::*;` LRM shorthand (re-export everything imported into the current scope,
  regardless of package) is not implemented. Only `export pkg::*` / `export pkg::item` are
  handled — see Phase 6.3 section above. That literal alternative doesn't route through the
  `package_import_item` grammar rule at all, so `SvRecordListener` silently ignores it.
- References, rename, and signature help providers still return `nullptr`. These are next
  after Phase 6.3/6.4.
- **Mid-line macro expansion corrupts columns (not lines) of symbols declared later on the
  same source line.** `SvPreprocessor`'s source map (`sourceMap`) only translates line
  numbers across macro expansion — it never adjusts columns for the text-length delta a
  macro invocation introduces mid-line. `sv_tree_walker.cpp`'s `pushId()` takes
  `tok->getCharPositionInLine()` directly from the *expanded* text with no correction.
  Example: `` wire [`WIDTH-1:0] data_bus; `` where `` `WIDTH `` (6 chars) expands to `8`
  (1 char) shifts `data_bus` 5 columns left of its true position in the original buffer.
  Symbols on macro-free lines are unaffected (confirmed via probe). Regression test added
  at `tests/integration/test_19_macro_midline_expansion.sh` /
  `fixtures/preproc_midline.sv` — asserts the *correct* (original-source) column and
  currently **fails** (2 of 5 cases) documenting this gap; line-number resolution,
  hover-by-name, and macro-free-line columns all pass. Fix would need per-output-line
  column-delta tracking in the source map, not just file/line — not yet designed.
- **Multi-line (backslash-continuation) `` `define `` bodies are not supported at all.**
  `SvPreprocessor::processSource` reads and expands strictly one physical line at a time
  (`std::getline` loop) with no check for a trailing `\` on a `` `define `` line — the
  continuation line is emitted as ordinary source code instead of being merged into the
  macro body, and the literal trailing backslash is left in the body text. Invoking such a
  macro produces a stray `\` in the expanded output, which the ANTLR parser then reports as
  a genuine syntax error (confirmed via probe: two spurious `parseErrors` for a
  two-line `` `define ``). This is on top of, and more severe than, the mid-line column-drift
  gap above. Regression test at `tests/integration/test_20_macro_multiline_midline.sh` /
  `fixtures/preproc_multiline_midline.sv` — asserts zero diagnostics and the correct
  original-source column for the symbol following the macro; currently **fails 3 of 6**
  cases (diagnostics non-empty; declaration and go-to-definition column wrong). Hover
  (name-based) and the macro-free control symbol still pass — ANTLR's error recovery is
  forgiving enough to keep producing *some* records despite the corrupted text. Fix needs
  the `processSource` line loop to detect a trailing `\` and concatenate continuation
  lines (stripping the backslash) before handing the merged line to `parseMacroDefinition`,
  plus emitting one blank output line per consumed continuation line to keep the source map
  1:1 — not yet designed.
