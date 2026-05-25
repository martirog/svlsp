# svlsp — Handoff Document

**Date:** 2026-05-25  
**Last completed phase:** Phase 3 complete (all ten sub-phases)  
**Current work:** Phase 4.1 — ANTLR4 grammar integration (tool detection done; grammar + runtime pending)

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
                   signature_help — LSP layer (Phase 3 complete)
src/compiler/      (empty — Phase 4, parser/listener go here)
src/db/            (empty — Phase 5)
src/main.cpp       entry point
tests/unit/        Catch2 unit tests (48 cases, 72 assertions)
tests/integration/ Emacs functional test scripts (31 test cases across 13 files)
tools/             emacs-test-daemon.sh, emacs-test-init.el, emacs-test-lib.sh
examples/          .sv fixture files (module_basic.sv, module_params.sv; 18 more in Phase 4.2)
grammar/           (empty — Sv.g4 goes here, Phase 4.1)
cmake/             CMake helper modules
  ANTLR4Tool.cmake  tool detection: PATH → antlr4 cmd; fallback → download JAR
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

# Unit tests (48 cases, 72 assertions)
make test-unit
# or: build/debug/unit_tests

# Integration tests (Emacs daemon, requires display or Xvfb)
DISPLAY=:99 make test-integration   # runs all 13 test files
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

Currently publishes empty diagnostics on `didOpen`/`didChange`. Phase 4 will pass real parse errors.

### Phase 3 LSP feature providers

Each lives in `src/lsp/<name>.h/.cpp` and follows the same pattern:
- `static getXxx(Params)` → result type — currently returns `nullptr` (JSON null)
- No stored state, no I/O dependencies → directly unit-testable without mocks

| File | Class | Method | Result type |
|---|---|---|---|
| `hover.h/.cpp` | `HoverProvider` | `getHover(HoverParams)` | `NullOr<Hover>` |
| `definition.h/.cpp` | `DefinitionProvider` | `getDefinition(DefinitionParams)` | `NullOrOneOf<Definition, Array<DefinitionLink>>` |
| `references.h/.cpp` | `ReferencesProvider` | `getReferences(ReferenceParams)` | `NullOr<Array<Location>>` |
| `completion.h/.cpp` | `CompletionProvider` | `getCompletion(CompletionParams)` | `NullOrOneOf<Array<CompletionItem>, CompletionList>` |
| `document_symbols.h/.cpp` | `DocumentSymbolsProvider` | `getDocumentSymbols(DocumentSymbolParams)` | `NullOrOneOf<Array<SymbolInformation>, Array<DocumentSymbol>>` |
| `workspace_symbols.h/.cpp` | `WorkspaceSymbolsProvider` | `getWorkspaceSymbols(WorkspaceSymbolParams)` | `NullOrOneOf<Array<SymbolInformation>, Array<WorkspaceSymbol>>` |
| `rename.h/.cpp` | `RenameProvider` | `getRename(RenameParams)` | `NullOr<WorkspaceEdit>` |
| `signature_help.h/.cpp` | `SignatureHelpProvider` | `getSignatureHelp(SignatureHelpParams)` | `NullOr<SignatureHelp>` |

### I/O wrapper (`src/lsp/server.h/.cpp`)

`LanguageServer` owns `m_connection`, `m_messageHandler`, `m_store`, and `m_diagnostics`.
Member declaration order matters: `m_diagnostics` must be declared after `m_messageHandler`.

`registerHandlers()` wires all message types:

| Message | Kind | Handler |
|---|---|---|
| `initialize` | request | `handleInitialize` — stores processId, returns capabilities |
| `initialized` | notification | `handleInitialized` — no-op |
| `shutdown` | request | `handleShutdown` — transitions to Shutdown |
| `exit` | notification | `handleExit` — transitions to Inactive, breaks run() loop |
| `textDocument/didOpen` | notification | `m_store.open()` → `m_diagnostics.publish()` |
| `textDocument/didChange` | notification | `m_store.update()` → `m_diagnostics.publish()` |
| `textDocument/didClose` | notification | `m_store.close()` |
| `textDocument/hover` | request | `HoverProvider::getHover()` |
| `textDocument/definition` | request | `DefinitionProvider::getDefinition()` |
| `textDocument/references` | request | `ReferencesProvider::getReferences()` |
| `textDocument/completion` | request | `CompletionProvider::getCompletion()` |
| `textDocument/documentSymbol` | request | `DocumentSymbolsProvider::getDocumentSymbols()` |
| `workspace/symbol` | request | `WorkspaceSymbolsProvider::getWorkspaceSymbols()` |
| `textDocument/rename` | request | `RenameProvider::getRename()` |
| `textDocument/signatureHelp` | request | `SignatureHelpProvider::getSignatureHelp()` |

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
| Parser generator | ANTLR4 (Phase 4, not started) | `plan.md` §4 |
| Database | SQLite3 (Phase 5, not started) | `plan.md` §5 |

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
| 3.2 | Diagnostics | `textDocument/publishDiagnostics` | Complete (empty push — real errors in Phase 4) |
| 3.3 | Hover | `textDocument/hover` | Complete (null — Phase 4) |
| 3.4 | Go-to-definition | `textDocument/definition` | Complete (null — Phase 4) |
| 3.5 | Find references | `textDocument/references` | Complete (null — Phase 4) |
| 3.6 | Completion | `textDocument/completion` | Complete (null — Phase 4) |
| 3.7 | Document symbols | `textDocument/documentSymbol` | Complete (null — Phase 4) |
| 3.8 | Workspace symbols | `workspace/symbol` | Complete (null — Phase 4) |
| 3.9 | Rename | `textDocument/rename` | Complete (null — Phase 4) |
| 3.10 | Signature help | `textDocument/signatureHelp` | Complete (null — Phase 4) |

---

## Phase 4 status — In progress

### 4.1 Grammar Integration — partially done

| Step | Status | Notes |
|---|---|---|
| ANTLR4 tool detection in CMake | **Done** | `cmake/ANTLR4Tool.cmake` — PATH first, JAR fallback |
| Fetch `Sv.g4` → `grammar/Sv.g4` | Pending | URL: `miguel-guerrero/antlr4_system_verilog_parser` |
| ANTLR4 C++ runtime (FetchContent) | Pending | `antlr/antlr4` `runtime/Cpp`, v4.13.2 |
| CMake `add_custom_command` for generation | Pending | Uses `${ANTLR4_TOOL_COMMAND} -Dlanguage=Cpp` |
| Unit test: parse minimal SV snippet | Pending | |

### 4.2–4.6 — Pending

4.2 Add 18 remaining `.sv` example files (module_basic.sv + module_params.sv exist).  
4.3 ANTLR4 listener / AST visitor.  
4.4 Symbol extraction (modules, ports, signals, functions, classes, macros).  
4.5 Error recovery → `lsp::Diagnostic` objects → `DiagnosticsPublisher`.  
4.6 Incremental parsing (depends on Phase 5 DB for file hashing).  

After Phase 4 is complete, replace `nullptr` returns in all Phase 3 providers with real symbol queries.

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
- All Phase 3 providers return `nullptr` (JSON null) until Phase 4 wires in the ANTLR4
  parser. Editors handle null responses gracefully — no popup, no jump, no list shown.
