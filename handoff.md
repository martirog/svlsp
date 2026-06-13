# svlsp — Handoff Document

**Date:** 2026-06-13  
**Last completed phase:** Phase 3 complete; Phase 4.1–4.6 complete; Phase 5.1–5.4 complete; Phase 6.1 complete  
**Current work:** Phase 6.2+ — multi-file project support, package import resolution, cross-file invalidation

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
                   schema — SQLite persistence layer (Phase 5 complete, schema v2)
src/main.cpp       entry point
tests/unit/        Catch2 unit tests (228 cases, 550 assertions)
tests/integration/ Emacs functional test scripts (15 files, ~45 test cases)
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
DISPLAY=:99 make test-integration   # runs all 15 test files
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
| Database | SQLite3 (amalgamation, schema v2) | `src/db/schema.h` |
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

Scope stack tracks Module/Interface/Package/Class/Function/Task — names are pushed on
enter hooks and popped on exit hooks. Each `ParseRecord` carries the `scope` chain
(e.g. `"MyModule::MyClass"`) and `endLine` (last line of scope body, for range building).
Exit hooks call `backpatchEndLine` to patch the record after the closing token is seen.

### 4.4 Symbol Extraction — Complete

`ParseRecord` fields: `kind, name, line, column, parent, detail, endLine, scope`.

- `endLine` — 1-based last line of scope body; 0 for leaf symbols (ports, signals, parameters)
- `scope` — full enclosing scope chain (e.g. `"MyModule::MyClass"`); `""` for top-level symbols

### 4.5 Error Recovery — Complete

`ParseError { line, column, message }`. `SvErrorListener` installed on lexer + parser.
`DiagnosticsPublisher::buildDiagnostic(ParseError)` converts to `lsp::Diagnostic`.

### 4.6 Incremental Parsing — Complete

`ParseCache` replaced by `CompilationController` (Phase 5.4) which uses SQLite hash storage.

---

## Phase 5 — SQLite Database Layer — Complete

### Schema v2

Defined in `src/db/schema.h`. `db::SCHEMA_VERSION = 2`.

Three tables:
- `files (id, path UNIQUE, content_hash, parsed_at)`
- `symbols (id, file_id→files, kind, name, line, col, parent, detail, end_line, scope)` —
  indexes on `name`, `file_id`, `scope`, `(scope,name)`, `(file_id,line,end_line)`
- `diagnostics (id, file_id→files, line, col, message)`

Migration `MIGRATION_V1_TO_V2` adds `end_line` and `scope` columns plus three new indexes;
`initSchema()` in `database.cpp` runs it automatically when `schemaVersion() < 2`.

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
| `findSymbolsVisibleAt(path, line) → vector<SymbolRow>` | UNION ALL: file-local symbols in scope chain + cross-file top-level symbols; C++ sorted by scope depth then name |

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
Unit test suite: **228 tests, 550 assertions**.

### Pending in Phase 6

| Sub-phase | Feature | Status |
|---|---|---|
| 6.2 | Multi-file project support (`compile_commands.json`) | Not started |
| 6.3 | Package import resolution (`import pkg::*`) | Not started — see `plan.md §6.3` |
| 6.4 | Cross-file invalidation (dependency graph) | Not started — see `plan.md §6.4` |
| 6.5 | Performance baseline | Not started |
| 6.6 | Packaging / `make install` | Not started |

**Phase 6.3 note:** `findSymbolsVisibleAt` currently pulls only `scope = ""` symbols from
other files (the package declarations themselves), not their contents. To fix:
1. Hook `enterPackage_import_item` in `SvRecordListener` to emit import records.
2. Add `imports (id, file_id, pkg_name, item)` table (`item = "*"` for wildcard).
3. Extend `findSymbolsVisibleAt` to consult the imports table and add the imported
   package scope(s) to the search list.

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
- References, rename, and signature help providers still return `nullptr`. These are next
  after Phase 6.3/6.4.
