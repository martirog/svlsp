# svlsp — Handoff Document

**Date:** 2026-05-28  
**Last completed phase:** Phase 3 complete (all ten sub-phases); Phase 4.1 complete; Phase 4.2 complete; Phase 4.2a complete; Phase 4.2b complete  
**Current work:** Phase 4.2c — Preprocessor tool selection and integration

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
tests/unit/        Catch2 unit tests (70 cases, 97 assertions — see counts below)
tests/integration/ Emacs functional test scripts (31 test cases across 13 files)
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
| Parser generator | ANTLR4 v4.13.2 (FetchContent) | `CMakeLists.txt` |
| Database | SQLite3 (Phase 5, not started) | `plan.md` §5 |
| SV directive taxonomy | Two-pass: strip compiler directives first, preprocess second | `docs/decisions/sv-preprocessor.md` (complete) |
| `__FILE__` / `__LINE__` | Resolved in pass 1 against original source, before include shifts line numbers | `plan.md §4.2b` |
| SV preprocessor tool | To be decided — Phase 4.2c | `plan.md §4.2c`, `docs/decisions/sv-preprocessor.md` (pending) |

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

Unit test count: 70 cases, 97 assertions (was 52/79 before Phase 4.2).

### 4.2a Directive Taxonomy and Scope — Complete

All IEEE 1800-2017 §22 backtick directives classified into two processing passes.
ADR created: `docs/decisions/sv-preprocessor.md` (taxonomy section complete; tool decision
added in §4.2c).

Two-pass pipeline:
- **Pass 1 — compiler directive strip (§4.2b):** metadata directives that do not transform
  text (`timescale, `default_nettype, `celldefine/`endcelldefine, `unconnected_drive/
  `nounconnected_drive, `resetall, `begin_keywords/`end_keywords, `pragma, `line).
  `__FILE__` and `__LINE__` are also resolved here — substituted with the original source
  path and line number before include insertions shift line counts or a temp buffer is
  created.
- **Pass 2 — preprocessor (§4.2c):** text-stream transformers (`define/`undef/`undefineall,
  `ifdef/`ifndef/`elsif/`else/`endif, `include, macro invocations).

### 4.2b Compiler Directive Strip Pass — Complete

Dependency-free, line-oriented C++ pass. Each stripped directive line is replaced with a
blank line so downstream passes see the same line numbers as the original source.
`__FILE__` and `__LINE__` are substituted inline before directive detection runs.

Files added:
- `src/compiler/compiler_directive_stripper.h` — `DirectiveKind` enum, `DirectiveRecord`
  struct `{ kind, value, line }`, `StripResult` struct, `CompilerDirectiveStripper` class
- `src/compiler/compiler_directive_stripper.cpp`
- `tests/unit/compiler/test_compiler_directive_stripper.cpp`

18 unit tests, 64 assertions. Full suite: 88 tests, 179 assertions.

### 4.2c Preprocessor Tool Selection and Integration — Pending

Evaluate candidates (slang preprocessor, verilator --preproc, sv-parser Rust FFI, minimal
in-house) and integrate the chosen tool between the §4.2b output and the ANTLR4 parser.

New files (exact names depend on chosen tool):
- `src/compiler/sv_preprocessor.h/.cpp` — wraps the chosen tool behind a common interface
- `tests/unit/compiler/test_sv_preprocessor.cpp`

Deliverable: tool decision appended to `docs/decisions/sv-preprocessor.md`.
Unit test: `` `define WIDTH 8 `` + `` wire [`WIDTH-1:0] bus; `` → `wire [8-1:0] bus;` with no
remaining backtick tokens.

### 4.3–4.6 — Pending

4.3 ANTLR4 listener / AST visitor.  
4.4 Symbol extraction (modules, ports, signals, functions, classes, macros).  
4.5 Error recovery → `lsp::Diagnostic` objects → `DiagnosticsPublisher`.  
4.6 Incremental parsing (depends on Phase 5 DB for file hashing).  

After Phase 4 is complete, replace `nullptr` returns in all Phase 3 providers with real symbol queries.

---

## Sv.g4 grammar quirks (discovered in Phase 4.2)

These are bugs or limitations in the `miguel-guerrero` grammar that fixture authors must work around:

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
- All Phase 3 providers return `nullptr` (JSON null) until Phase 4 wires in the ANTLR4
  parser. Editors handle null responses gracefully — no popup, no jump, no list shown.
