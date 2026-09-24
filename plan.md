# SystemVerilog Language Server — Project Plan

## Overview

This project implements a SystemVerilog Language Server Protocol (LSP) server in C++.
It provides IDE features (completions, go-to-definition, hover, diagnostics, etc.) for
SystemVerilog source files by combining an ANTLR4-based compiler front-end with an
SQLite-backed incremental compilation database, exposed over the LSP protocol.

Testing is automated end-to-end using Emacs running in daemon mode with lsp-mode.

---

## Guiding Principles

- Every function gets a unit test before it is merged.
- Every LSP feature must have both a **unit test** (isolated, no network, no Emacs) and
  a **functional test** in which the `svlsp` binary is running and Emacs transmits a real
  LSP JSON-RPC command over the wire and the response is verified. Neither test alone is
  sufficient.
- Every function or feature gets two consecutive commits:
  1. **Implementation commit** — code + unit test, commit message explains the function.
  2. **Documentation commit** — updates to `docs/` reflecting what was done, how to test
     it, and how to use it.
- All code is commented at a level sufficient for code review (the *why*, not just the
  *what*).
- No functional code is written until its unit test exists.
- Functional/Emacs tests follow unit tests and must also pass before merging.

---

## Phase 0 — Project Infrastructure

**Goal:** Establish the repository, build system, and documentation skeleton before any
feature code is written.

### 0.1 Git Repository
- Initialise a git repository in the project root.
- Add a `.gitignore` covering C++ build artefacts, CMake cache, SQLite WAL files, and
  Emacs lock files.
- Commit the initial skeleton (this plan, Makefile, CMakeLists stub).

### 0.2 Directory Layout

```
svlsp/
├── plan.md               # This document
├── Makefile              # Builds plan PDF/HTML; delegates C++ build to CMake
├── CMakeLists.txt        # Top-level CMake
├── docs/                 # Evolving documentation (generated from Markdown sources)
│   ├── index.md
│   └── decisions/        # Architecture decision records
├── src/
│   ├── lsp/              # LSP protocol layer
│   ├── compiler/         # SystemVerilog ANTLR4 compiler
│   ├── db/               # SQLite database layer
│   └── main.cpp          # Entry point
├── tests/
│   ├── unit/             # Catch2 unit tests, mirroring src/
│   └── integration/      # Emacs/lsp-mode end-to-end functional tests
├── grammar/              # Sv.g4 and any grammar patches
├── examples/             # SystemVerilog example files (one per language feature)
└── tools/                # Helper scripts (Emacs elisp test runner, etc.)
```

### 0.3 Build System (CMake)
- CMake ≥ 3.20, C++20.
- Targets: `svlsp` (server binary), `unit_tests` (test runner), ANTLR4 generated sources.
- Integrate ANTLR4 C++ runtime via CMake FetchContent or a vendored submodule.
- Integrate SQLite (amalgamation or system package).
- Integrate a C++ unit-test framework (Catch2 recommended; decide in Phase 2).
- Integrate lsp-framework (see Phase 2 evaluation).
- `cmake --preset debug` and `cmake --preset release` presets defined.

### 0.4 Documentation Toolchain
- `plan.md` (this file) and per-phase docs in `docs/` are the canonical documentation.
- `Makefile` targets:
  - `make docs-html` — converts all Markdown to HTML (via `pandoc`).
  - `make docs-pdf`  — converts all Markdown to PDF (via `pandoc` + LaTeX or `wkhtmltopdf`).
  - `make docs`      — builds both.
- A `docs/index.md` serves as the documentation entry point.

---

## Phase 1 — Emacs Daemon Test Infrastructure

**Goal:** Build the functional test harness before writing any LSP handler code, so that
the first working connection can be verified against a real Emacs client immediately.

### 1.1 Emacs Daemon Setup
- Document how to start Emacs in daemon mode: `emacs --daemon=svlsp-test`.
- Document how to connect: `emacsclient -s svlsp-test`.
- Provide a shell script `tools/emacs-test-daemon.sh` that:
  1. Starts the daemon with a minimal, reproducible init file.
  2. Loads `lsp-mode` and points it at the local `svlsp` binary.
  3. Opens a test SystemVerilog file.
  4. Queries LSP results via `emacsclient -e`.
  5. Compares results against expected output.
  6. Kills the daemon and reports pass/fail.

### 1.2 Emacs Test Init File (`tools/emacs-test-init.el`)
- Minimal `init.el` that installs/loads only `lsp-mode` and `lsp-ui` (no other packages).
- Registers `svlsp` as the LSP server for SystemVerilog files.
- Defines helper Elisp functions used by the test harness.

### 1.3 Test Runner Targets
- `make test-integration` — runs `tools/emacs-test-daemon.sh`.
- `make test-unit` — runs the C++ unit test binary.
- `make test` — runs unit tests first, then functional tests.

---

## Phase 2 — LSP Server Framework Evaluation and Setup

**Goal:** Select and integrate a C/C++ LSP server library; achieve a minimal server binary
that Emacs can connect to and exchange `initialize`/`shutdown` messages with.

### 2.1 Evaluate C++ LSP Frameworks
Investigate and document the following candidates:

| Candidate | Notes |
|-----------|-------|
| [lsp-framework](https://github.com/leon-bckl/lsp-framework) | Header-only, modern C++, primary candidate |
| clangd internal LSP lib | Tightly coupled to clangd; likely not reusable |
| Any other active C++ LSP libraries found during research | TBD |

Decision: if no superior alternative is found, adopt **lsp-framework**. Record rationale
in `docs/decisions/lsp-framework.md`.

### 2.2 Integrate Chosen Framework
- Add as a git submodule or via CMake FetchContent.
- Write a smoke-test target that compiles and links against the library.
- Unit-test any thin wrappers written around the framework's API.

### 2.3 Minimal LSP Server (`initialize` / `shutdown`)
- Implement only the LSP lifecycle methods: `initialize`, `initialized`, `shutdown`, `exit`.
- Transport: stdio (standard for LSP; simplest to test).
- Unit tests: message serialisation/deserialisation, handler dispatch.
- Functional test: Emacs connects, lsp-mode reports the server as running, Emacs sends
  `initialize` and receives a valid `InitializeResult` over the wire.
- The resulting binary should also be exercisable with a hand-crafted JSON-RPC request
  piped to stdin.

### 2.4 Exercise All lsp-framework Features
- Work through every public API in lsp-framework systematically.
- For each capability: write a unit test and a corresponding functional test using Emacs.
- Document gaps or bugs found in `docs/decisions/lsp-framework.md`.

---

## Phase 3 — LSP Feature Implementation (Protocol Layer)

**Goal:** Implement the full set of LSP features required for SystemVerilog development,
one at a time, each with a unit test and an Emacs functional test.

Each sub-phase follows the same pattern:
1. Unit test (C++, isolated).
2. Implementation.
3. Functional test (Emacs sends real LSP command, response verified).
4. Two commits: implementation + documentation.

### 3.1 Text Document Synchronisation
- `textDocument/didOpen`, `didChange`, `didClose`.
- In-memory document store (maps URI → text content + version).

### 3.2 Diagnostics (`textDocument/publishDiagnostics`)
- Push diagnostics when a document is opened or changed.
- Initially: parse errors from the ANTLR4 compiler (Phase 4).
- Functional test: open a file with a syntax error; verify Emacs shows the diagnostic.

### 3.3 Hover (`textDocument/hover`)
- Return symbol information at cursor position by querying the database (Phase 5).
- Functional test: position cursor on a known symbol; verify hover text in Emacs.

### 3.4 Go-to-Definition (`textDocument/definition`)
- Resolve the definition location of a symbol under the cursor.
- Functional test: trigger go-to-definition in Emacs; verify jump to correct file/line.

### 3.5 Find References (`textDocument/references`)
- List all references to a symbol across the project.
- Functional test: trigger find-references; verify the reference list in Emacs.
- **Implemented 2026-09-13** — see §6.22 for the full writeup.

### 3.6 Completion (`textDocument/completion`)
- Context-aware completions: keywords, module ports, signal names, macros.
- Functional test: trigger completion at a known context; verify candidate list in Emacs.
- **Not yet done:** `CompletionProvider::getCompletion` (`src/lsp/completion.cpp`) currently
  filters `db.findSymbolsVisibleAt(path, line1)` with a strict prefix match
  (`row.name.compare(0, prefix.size(), prefix) != 0`) — no typo tolerance, no
  subsequence/fuzzy matching. Add fuzzy matching over the same visible-scope
  candidate set (e.g. subsequence matching with a relevance score, VSCode-style),
  so completions still surface when the typed prefix has a typo or skips
  characters. Score-and-sort the filtered results instead of preserving DB
  order. Functional test: trigger completion with a misspelled/partial,
  non-prefix-matching input; verify the intended symbol still appears.

### 3.7 Document Symbols (`textDocument/documentSymbol`)
- Return the symbol outline for a file (modules, interfaces, functions, tasks, etc.).
- Functional test: request document symbols; verify outline matches example file.

### 3.8 Workspace Symbols (`workspace/symbol`)
- Search symbols across the whole project.
- Functional test: search for a known symbol name; verify result in Emacs.

### 3.9 Rename (`textDocument/rename`)
- Rename a symbol and all its references across the project.
- Functional test: rename a signal; verify all occurrences updated in Emacs buffers.
- **Implemented 2026-09-13** — see §6.22 for the full writeup.

### 3.10 Signature Help (`textDocument/signatureHelp`)
- Show port/parameter signatures when editing a module instantiation.
- Functional test: type a module instantiation; verify signature popup in Emacs.
- **Implemented 2026-09-13** — see §6.22 for the full writeup.

---

## Phase 4 — SystemVerilog ANTLR4 Compiler Front-End

**Goal:** Parse SystemVerilog source files into an AST using ANTLR4 and extract semantic
information into the database.

**This phase can proceed in parallel with Phase 3.**

### 4.1 Grammar Integration
- Copy `Sv.g4` from
  `https://github.com/miguel-guerrero/antlr4_system_verilog_parser/blob/master/Sv.g4`
  into `grammar/Sv.g4`.
- Generate C++ lexer and parser from the grammar as part of the CMake build.
- Unit test: parse a minimal SystemVerilog file and verify the parse tree root.

### 4.2 SystemVerilog Example Library (`examples/`)
One example `.sv` file per major language feature, used as test fixtures:

| Example File | Feature Covered |
|---|---|
| `module_basic.sv` | Module declaration, ports |
| `module_params.sv` | Parameters and localparams |
| `interfaces.sv` | Interface declaration and modport |
| `always_blocks.sv` | `always_ff`, `always_comb`, `always_latch` |
| `functions_tasks.sv` | Functions and tasks |
| `classes.sv` | OOP classes, inheritance |
| `packages.sv` | Package declaration and import |
| `structs_unions.sv` | Structs and unions |
| `enums.sv` | Enum types |
| `generate.sv` | Generate blocks (for/if) |
| `assertions.sv` | SVA — concurrent and immediate assertions |
| `clocking.sv` | Clocking blocks |
| `coverage.sv` | Covergroups and coverpoints |
| `constraints.sv` | Randomisation and constraints |
| `macros.sv` | Preprocessor macros `` `define``, `` `include`` |
| `timescale.sv` | `` `timescale`` and time literals |
| `bind.sv` | `bind` construct |
| `program.sv` | Program blocks |
| `checker.sv` | Checker blocks |
| `dpi.sv` | DPI-C imports/exports |

### 4.2a Directive Taxonomy and Scope

**Goal:** Classify every IEEE 1800 backtick directive into one of two processing passes
and define the LSP scope for each. Deliverable: taxonomy table committed as the opening
section of `docs/decisions/sv-preprocessor.md` (tool decision recorded in §4.2c).

IEEE 1800-2017 §22 calls all backtick directives "compiler directives", but from an
implementation standpoint they split into two fundamentally different kinds:

**Pass 1 — compiler directive strip:** directives that change compilation metadata/state
but do NOT transform the text stream. Handled by a simple line-oriented pass (§4.2b)
before any preprocessor runs. `__FILE__` and `__LINE__` are also resolved here, against
the original source file, so they expand to the real filename and the real source line
number — before `include insertions shift line counts and before any temp buffer is created.

| Directive | Description |
|---|---|
| `` `timescale <unit>/<prec> `` | Time unit and precision — strip, record value |
| `` `default_nettype <type> `` | Default net type for implicit wires — strip, record value |
| `` `celldefine `` / `` `endcelldefine `` | Mark/unmark module as a cell — strip |
| `` `unconnected_drive pull0\|pull1 `` / `` `nounconnected_drive `` | Unconnected port drive — strip |
| `` `resetall `` | Reset all compiler-directive state — strip |
| `` `begin_keywords "version" `` / `` `end_keywords `` | Keyword set selection — strip (assume SV-2017) |
| `` `pragma `` | Tool-specific hints — strip |
| `` `line N "file" level `` | Source location override — strip |
| `` `__FILE__ `` | Substitute with original source filename (not a temp buffer path) |
| `` `__LINE__ `` | Substitute with line number in original source (before include expansion) |

**Pass 2 — preprocessor:** directives that transform the token stream; the ANTLR4 parser
never sees them. Handled by the chosen preprocessor tool (§4.2c).

| Directive | Description |
|---|---|
| `` `define NAME[(args)] body `` | Macro definition |
| `` `undef NAME `` | Remove a named macro |
| `` `undefineall `` | Remove all macros |
| `` `ifdef `` / `` `ifndef `` / `` `elsif `` / `` `else `` / `` `endif `` | Conditional compilation |
| `` `include "file" `` | File inclusion — pastes content into token stream |
| `` `NAME `` (invocation) | Macro expansion |

### 4.2b Compiler Directive Strip Pass

**Goal:** Implement a simple, dependency-free C++ pass that removes compiler directives
and resolves `__FILE__`/`__LINE__` from SV source before it reaches the preprocessor or
parser.

Compiler directives do not nest and do not transform text — a line-oriented lexer is
sufficient. The pass records directives that carry semantic values (e.g. `default_nettype`)
so later phases can query them.

```
raw SV source  +  original file path
    └─(CompilerDirectiveStripper)
        ├─ cleaned source (`__FILE__/`__LINE__ substituted; metadata directives removed)
        └─ DirectiveRecord[] { kind, value, location }
```

Implementation: `src/compiler/compiler_directive_stripper.h/.cpp`

Unit tests (`tests/unit/compiler/test_compiler_directive_stripper.cpp`):
- `` `timescale 1ns/1ps `` removed from output; recorded in DirectiveRecord
- `` `default_nettype none `` removed and recorded
- `` `celldefine `` / `` `endcelldefine `` stripped
- `` `resetall `` stripped
- `` `begin_keywords "1800-2017" `` / `` `end_keywords `` stripped
- `` `__FILE__ `` replaced with the original file path string literal
- `` `__LINE__ `` replaced with the decimal line number from original source
- `__FILE__` value is the path passed in, not any temp-buffer path
- Non-directive lines pass through unchanged
- Multiline source with directives interspersed: output matches expected stripped form

### 4.2c Preprocessor Tool Selection and Integration

**Goal:** Select and integrate a tool to resolve pass-2 preprocessor directives (macro
expansion, conditional compilation, file inclusion) against the cleaned source from §4.2b.

Candidates:

| Candidate | Language | Licence | Notes |
|---|---|---|---|
| `slang` preprocessor | C++ | MIT | Full IEEE 1800-2017; `slang::parsing::Preprocessor` is embeddable |
| `verilator --preproc` | C++ | LGPL | Mature; spawnable as a filter subprocess |
| `sv-parser` | Rust | MIT | Full SV 2017 with preprocessing; requires C FFI wrapper |
| Minimal in-house | C++ | — | Handle `` `define/undef/undefineall/ifdef/include `` + macro invocation |

Evaluation criteria: C++ embeddability, licence compatibility, recursive macro support,
stringification (`` `" ``) and token-pasting (`` `​`` `​`` ``), `` `include `` path
resolution, function-like macros with arguments.

New files (exact names depend on chosen tool):
- `src/compiler/sv_preprocessor.h/.cpp` — wraps the chosen tool behind a common interface
- `tests/unit/compiler/test_sv_preprocessor.cpp`

Deliverable: tool decision appended to `docs/decisions/sv-preprocessor.md`.
Unit test: source string with `` `define WIDTH 8 `` + `` wire [`WIDTH-1:0] bus; ``; output
contains `wire [8-1:0] bus;` with no remaining backtick tokens.

### 4.3 AST Visitor / Listener
- Implement a C++ ANTLR4 listener that walks the parse tree and emits structured records.
- Unit test each listener callback against the corresponding example file.

### 4.4 Symbol Extraction
- Extract: module/interface/package names, port declarations, signal declarations,
  function/task signatures, class hierarchies, macro definitions.
- Unit test each extractor with targeted example snippets.

### 4.5 Error Recovery
- ANTLR4 error listener that converts parse errors to LSP `Diagnostic` objects.
- Unit test with intentionally malformed SystemVerilog snippets.

### 4.6 Incremental Parsing
- Re-parse only files that have changed since the last compilation.
- The database (Phase 5) tracks file modification hashes to drive this.

---

## Phase 5 — SQLite Database Layer

**Goal:** Persist compilation artefacts so the LSP server can answer queries without
re-parsing, and so only changed files need recompilation.

**This phase can proceed in parallel with Phases 3 and 4.**

### 5.1 Database Schema Design
Tables (initial design; may evolve):

| Table | Purpose |
|---|---|
| `files` | File path, last-modified hash, parse timestamp |
| `modules` | Module name, file, line, column |
| `ports` | Port name, direction, type, parent module |
| `signals` | Signal name, type, scope, file, line |
| `functions` | Function/task name, signature, file, line |
| `classes` | Class name, parent class, file, line |
| `packages` | Package name, file, line |
| `references` | Symbol name, use site (file, line, col) |
| `macros` | Macro name, definition, file, line |
| `diagnostics` | File, line, col, severity, message |

Schema migrations are versioned (a `schema_version` table).

### 5.2 Database Abstraction Layer (`src/db/`)
- A C++ class wrapping SQLite3 (RAII connection, prepared statements).
- Unit test: open, insert, query, close; schema migration; error handling.

### 5.3 Query API
- A `SymbolDatabase` class with typed query methods used by the LSP layer:
  - `findDefinition(uri, line, col) -> Location`
  - `findReferences(symbolName) -> vector<Location>`
  - `getHover(uri, line, col) -> string`
  - `getCompletions(uri, line, col) -> vector<CompletionItem>`
  - etc.
- Unit test each query method against a pre-populated in-memory SQLite database.

### 5.4 Incremental Compilation Controller
- On file open/change: hash the file content, compare against `files` table.
- If changed (or new): re-parse, update all affected tables, remove stale rows.
- If unchanged: skip parsing, serve data from database.
- Unit test: simulate file change detection and verify only affected tables are updated.

---

## Phase 6 — End-to-End Integration and Polishing

**Goal:** Wire all layers together, run the full Emacs functional test suite, and fix
any gaps discovered.

### 6.1 Full Functional Test Suite
- One Emacs functional test per LSP feature in Phase 3, using example files from Phase 4.
- Tests run headless via `make test-integration`.

### 6.2 Multi-File Project Support — IN PROGRESS (Stage 1/6 complete, commit `fe0f817`)

**Resolved (was Appendix C, open question 5):** support *both* a custom, extensible
`.svlsp.json` manifest *and* a VCS/Questa/Xcelium-style `.f` filelist (for interop with
existing EDA build flows), both producing one shared `ProjectConfig` struct. The
filelist parser implements full `-y`/`-v`/`+libext+` library resolution (not a stub),
and errors hard on any unsupported filelist switch. Unresolved instantiations emit a
diagnostic via the existing `ParseError` pipeline.

**Full staged implementation plan (exact signatures, schema SQL, the library-resolution
fixpoint algorithm, file/test names, PR sequencing) lives at
`/home/martin/.claude/plans/fluffy-hatching-popcorn.md` — read that file to resume.**
Stage status is tracked in `handoff.md`'s "Phase 6.2" section; Stage 1 (Program
declaration tracking + `InstantiationRecord` + schema v5) is complete. Stages 2-6
(filelist parser, JSON manifest parser, config-threading + library resolver, server
wiring/discovery, end-to-end integration test) are not started.

- Compile switches supported per-project and per-file:
  - `-D NAME[=VALUE]` / `+define+NAME[=VALUE]` — preprocessor defines (passed to the SV preprocessor from §4.2a)
  - `-I DIR` / `+incdir+DIR` — include search directories for `` `include `` resolution
  - `--top MODULE` / `-top MODULE` — root module for elaboration
  - `--sv` / `--v` / `-sv` / `-sverilog` — force SystemVerilog or Verilog 2005 mode
  - `-y DIR` / `-v FILE` / `+libext+.ext` — library-based module resolution (new scope, see plan file)
- Batch-compile all listed files at server startup; background re-compile on change.
- `CompilationController` (§5.4 — this is the actual class; `CompilationDriver` below was
  an earlier planning-doc name for the same thing) reads these switches from the project
  file and threads them through the preprocess → parse → extract pipeline.

### 6.3 Package Import Resolution

**Why this is needed:** After `import pkg::MyClass` or `import pkg::*`, the identifier
`MyClass` must resolve to `pkg::MyClass` without the `pkg::` prefix. The current
`findSymbolsVisibleAt` only pulls `scope = ""` symbols from other files (i.e. the
packages themselves), not their contents. A class inside a package has
`scope = "pkg"` and is invisible at the caller's scope chain unless imports are tracked.

**What is missing:**
- `SvRecordListener` must hook `enterPackage_import_item` (grammar rule 696) and emit
  an `ImportRecord { pkgName, itemName /* or "*" */, line }`.
- A new DB table `imports (id, file_id → files, pkg_name, item TEXT)` where
  `item = "*"` means wildcard.
- `findSymbolsVisibleAt(path, line)` must consult `imports` for the current file:
  - `import pkg::ClassName` — add scope `"pkg"` to the search list, filtered to
    that single name (or add `pkg::ClassName` directly to a candidate set).
  - `import pkg::*` — add scope `"pkg"` to the search list unconditionally.

**Integration tests (once Phase 6 LSP providers are wired):**
- `test_15_import_single.sh`: open a file that does `import util_pkg::MyClass`, hover
  over `MyClass`, verify the definition points into `util_pkg.sv`.
- `test_16_import_star.sh`: `import util_pkg::*`, request completion inside the module,
  verify all package-level symbols appear in the list.

### 6.4 Cross-File Invalidation

**Status:** implemented 2026-09-12, scoped to `` `include `` edges only — see
the "Note" and "Implemented as" below for exactly why, and §6.21 for the
edge kind deliberately left out.

**Why this is needed:** `CompilationController` is single-file: it recompiles a file
when its own content hash changes, but does not detect that other files referencing its
symbols may now be broken. If `MyClass` in `a.sv` loses a field and `b.sv` uses that
field, editing `a.sv` must eventually flag `b.sv` as stale and re-check it.

**Original sketch (superseded by "Implemented as" below, kept for history):**
a `file_dependencies` table `(dependent_file_id → files, dependency_file_id →
files)` populated by a general "reference resolution" pass, walked after every
`compile()` call to recursively recompile every dependent. This predates
imports/instantiations tracking and assumed a resolution pass that doesn't
exist in this codebase — see the note directly below.

**Note, added 2026-09-12 (research pass ahead of implementation):** as things
stand today, only two edge kinds can actually leave a diagnostic stale:
`` `include `` edges (B's compiled unit literally embeds A's text) and
instantiation edges (`LibraryResolver`'s "unresolved instantiation" check).
**Import edges (`import pkg::X`) are deliberately out of scope for this
section, not because they're already handled, but because there is currently
no diagnostic at all that checks whether an imported or declared-type
reference actually resolves to a real declaration** — removing `X` from `pkg`
today produces silence (hover/completion just fail closed), not a stale
error, so there is nothing for cross-file invalidation to refresh. §6.21
proposes closing that gap; if it ships, its own reference-resolution edges
need exactly the invalidation treatment described below for `` `include ``
edges — same name-based-diff shape, different name set. **Instantiation
edges are also deliberately deferred out of this pass**, for a related but
distinct reason found in the same research: `SymbolDatabase::replaceDiagnostics`
does an unconditional `DELETE FROM diagnostics WHERE file_id=?` shared with
`LibraryResolver::appendDiagnostics`'s own rows, so a live edit already
silently wipes a file's "unresolved instantiation" diagnostic today with no
mechanism to re-add it — building instantiation-edge invalidation on top of
that without fixing it first would just paper over a bug rather than close
it. §6.21's own writeup covers the shared `source`-column fix both need.

**Implemented as (`` `include `` edges only):**
- New table, schema v8: `file_includes(includer_file_id, included_file_id)`
  (`src/db/schema.h`). `CompilationController::compile` (`src/db/compilation_controller.cpp`)
  persists one row per file in its own already-computed, fully-transitive
  include set (the same `includedFiles` list the diagnostics-visibility fix
  builds) via `SymbolDatabase::replaceFileIncludes` — on every real recompile,
  never on a cache hit, and unconditionally (not gated on whether a caller
  asked for the `includedFiles` out-param). Because one compile of a
  top-level file already discovers its *entire* include tree, a reverse
  lookup (`SymbolDatabase::includersOf(path)`) needs no recursion to find
  every file that would need recompiling if `path` changes — it's a single
  flat query.
- `CompilationController::compile` gained a `bool forceRecompile = false`
  parameter: unconditionally skips the content-hash cache-hit check.
  Required because a dependent file's own text can be completely unchanged
  even though something it `` `include ``s just changed — the ordinary hash
  comparison alone would wrongly treat that as a cache hit and skip
  re-parsing it.
- `LanguageServer`'s `textDocument/didSave` handler (`src/lsp/server.cpp`),
  right after its existing §6.18 primary-file recompile, looks up
  `includersOf(savedPath)` and schedules each result on a **second**
  `ChangeDebouncer` instance, `m_dependencyRechecker` (reusing the same class
  §6.8 already built rather than inventing a new queue — its per-key
  coalescing is exactly right here too). Its own fire callback,
  `forceRecompileAndPublish(path)`, reads current text from `m_store` if
  `path` is open (attaching its real client-tracked version) or from disk if
  not (no version — never `didOpen`ed), force-recompiles, and publishes its
  diagnostics plus every one of *its* included files' own (reusing the
  diagnostics-visibility fix's `collectIncludedDiagnostics` helper, factored
  out of `compileAndPublish` for this reuse).
- **Deliberately gated to `didSave`, not `didChange`** — directly validating
  the concern that motivated writing this section in the first place: a
  widely-`` `include ``d file (a shared macros header) could have many
  includers, and recompiling all of them synchronously, or on every
  keystroke-pause, would either stall the server or flood it with cascading
  recompiles during active typing. Save is deliberate and infrequent, exactly
  the reasoning §6.18's own text already established for this same handler.
- **Never holds `m_dataMutex` across the whole cascade.** Each queued
  dependent is an independent `ChangeDebouncer`-fired call: acquire the lock,
  force-recompile *one* file, publish, release. An interactive request
  (hover/completion/...) waiting on the same lock only ever waits behind one
  file's compile time, never the whole fan-out — `ChangeDebouncer::run`'s own
  loop already unlocks its *own* internal mutex before firing each due
  callback in sequence, and `m_dataMutex` itself is only held per-call inside
  `forceRecompileAndPublish`, not across the loop.

**A non-obvious testing subtlety, worth remembering for anyone extending
this:** diagnostics are attributed to the file a problem actually occurs in,
not to every file that transitively includes it (the same per-file
attribution the diagnostics-visibility fix already established) — so
breaking `` `include ``d file A does **not** change includer B's own
diagnostic *count* (it stays 0, correctly). The observable proof B was
genuinely force-recompiled is that B's own recompile re-discovers A as one of
its own included files and republishes A's diagnostics a *second* time (a
version-less echo, distinct from A's own client-tracked publish) — this is
what the unit tests below actually assert, after an initial, wrong attempt
assumed B's own count would change.

**Unit tests (implemented):** `tests/unit/db/test_symbol_database.cpp`
(`replaceFileIncludes`/`includersOf` round-trip, multiple includers of the
same file, overwrite-on-recompile); `tests/unit/db/test_compilation_controller.cpp`
(a real compile persists the edge; persisted independent of whether
`includedFiles` is requested; untouched on a cache hit; `forceRecompile`
genuinely bypasses the cache — proven the same sentinel-symbol way §5.4's own
cache-hit test does, in reverse); `tests/unit/db/test_database.cpp` (schema
creates `file_includes`, schema version 8). New full-stack file
`tests/unit/lsp/test_server_cross_file_invalidation.cpp` (real pipe
transport, same harness shape as `test_server_debounce.cpp`): saving an
included file force-recompiles its one includer (proven via the echo
mechanism above); editing without saving produces no propagation at all
(`hasPendingMessage` proving *no* further message arrives, not just "none for
this URI"); a widely-included file force-recompiles every one of several
includers. Unit suite now at 2000 assertions / 603 test cases, no
regressions.

**Functional test (implemented):** `tests/integration/test_38_cross_file_invalidation.sh`
— against two scratch temp files (A, and B which `` `include ``s A) outside
`SVLSP_ROOT`: dynamically shadows lsp-mode's own `lsp-diagnostics-updated-hook`
with a counting function for the duration of an edit+save on A, and confirms
at least two diagnostics-updated events fire (A's own save-triggered
recompile, plus at least one more from B's asynchronous force-recompile) —
a plain single-file save with no dependents would only ever produce one. This
proxy (an event count, not exact wire-message inspection) was chosen because
`(lsp-diagnostics)` alone can't distinguish "B was recompiled and found
nothing new" from "B was never touched" the way the unit test's version-field
check can; the precise mechanism is already proven directly at the unit
level, so the functional test's job is confirming the wiring reaches a real
client, not re-deriving that proof.

### 6.5 Performance Baseline
- Measure and document: time to parse a large SystemVerilog file, time to answer a
  `definition` query, memory footprint.
- Set minimum acceptable thresholds; add a `make benchmark` target.
- **Open question:** today `svlsp` is stdio-only (`src/main.cpp` — spawned by the
  client, reads/writes stdin/stdout, no CLI args), so the full project compile
  (e.g. ~7-8 min for the full UVM corpus per the handoff's session-3 finding)
  only starts once the editor spawns the process and sends `initialize`/
  `didOpen`. Would a standalone-process-plus-port model (start `svlsp` ahead of
  time as a long-running daemon listening on a TCP/Unix-domain socket, editor
  connects to it rather than spawning it) let the initial project compile begin
  in parallel with editor/session startup instead of serialized after it, and is
  that win worth the added complexity (daemon lifecycle management, port/socket
  discovery, multi-client handling if more than one editor window connects)?
  Revisit once a real large-project compile-time baseline exists here to
  quantify the actual win.
- **Open question:** taking the standalone-daemon idea above further — instead
  of just starting the daemon slightly ahead of the editor within one session,
  would running `svlsp` as a **long-lived, pre-warmed background instance**
  (started once, e.g. every morning before work begins, or kept running
  continuously — independent of any particular editor session's lifetime)
  help with both this section's compile-latency concerns *and* §6.8's
  debounce/async-compile problem, for large real codebases (UVM-scale)?
  Potential wins to weigh: (a) the full-project compile (~7-8 min for the full
  UVM corpus) happens once, off the clock, before anyone opens an editor, so
  no session ever pays it; (b) multiple editor windows/instances working on
  the same codebase could share one already-warm compiled DB instead of each
  independently recompiling the whole project on its own `initialize` (only
  possible once the DB is file-backed/shared — see the very next open
  question below); (c) a persistent process could keep §6.8's debounce
  scheduler "hot" and absorb a burst of edits/recompiles without ever paying
  cold-start cost, and could even keep compiling/refreshing diagnostics for
  files not currently open in any editor. Costs to weigh: daemon lifecycle
  management (who starts/stops/restarts it — a cron job, a systemd user
  service, a manual habit — and what happens to open editors if it crashes or
  is restarted mid-day), staleness if files change on disk outside any
  connected editor's `didChange` notifications (would need filesystem
  watching, which nothing in this codebase does today), and whether
  multi-client/multi-workspace support is even worth building versus just
  making a single editor session's own cold-start faster (the DB-persistence
  open question directly below is a strict subset of this idea and may
  capture most of the win at far lower complexity). Revisit alongside the
  daemon-plus-port question above once a real compile-time baseline exists to
  quantify how much this would actually save versus its added operational
  complexity.
  **Prior art confirmed (investigated 2026-08-28):** this is the standard
  shape for large-codebase build/dev tooling, not a novel idea — Bazel and
  Buck2 both run a persistent background daemon per workspace precisely to
  keep an in-memory dependency graph and caches warm across many separate
  command invocations, rather than re-deriving them from scratch each time;
  Buck2 daemons are shared across client processes keyed by workspace
  ("isolation directory") identity, directly analogous to "multiple editor
  windows on the same codebase share one warm `svlsp`" above. The lifecycle
  question this plan flags (start/stop/restart ownership, staleness on
  external file changes) is exactly what those tools' own daemon-management
  layers solve — worth reading how Bazel/Buck2 handle daemon restart and
  cache invalidation as a model rather than reinventing it, if this is ever
  pursued.
- **Open question:** the `Database`/`SymbolDatabase` layer already supports
  opening against a real file path (`Database` constructor,
  `src/db/database.cpp:11`), but the actual server today always opens
  `":memory:"` (`src/lsp/server.h:35` — "in-memory for now; file path in Phase
  6"), so every restart re-runs the full compile from scratch (again, ~7-8 min
  for the full UVM corpus). If that startup cost proves too slow in practice,
  is it worth switching to an on-disk DB file that persists across server
  restarts — reusing cached symbols/diagnostics for files whose content hash
  (`CompilationController::hashContent`) hasn't changed, only re-parsing what's
  actually stale — instead of (or alongside) the standalone-daemon idea above?
  Tradeoffs to weigh: disk-cache invalidation correctness (stale entries for
  files the DB never revisits), disk space/location conventions, and whether it
  meaningfully compounds with the daemon-plus-port question above versus being
  a simpler independent win. **See also §6.20** — external programs reading the
  live project's structural data (for analysis, dashboards, custom tooling)
  needs this same file-backed switch as its own prerequisite, independent of
  the restart-persistence motivation above.
- **Open question, needs investigation — pre-built/shared DBs for rarely-changing
  library code (UVM, verification IP).** **See §6.19 for a concrete design
  (drafted 2026-09-04), choosing the attach-and-query shape below over a true
  merge, per this bullet's own "Recommended default" conclusion.** A large
  fraction of a real verification
  project's total file count (per `handoff.md`'s UVM-corpus work, ~140 files) is
  third-party or internal library code that changes rarely — new UVM/VIP
  releases, not every edit — but gets fully recompiled from scratch by every
  project that includes it, and again on every server restart while the DB stays
  `:memory:` (see the open question directly above). Since the same library is
  typically reused unmodified across many projects, could `svlsp` ship or build
  **one pre-compiled DB for a given library version once**, and have individual
  projects reuse it instead of each recompiling all ~140 UVM files themselves?
  Two shapes worth investigating, not yet designed:
  - **Attach-and-query, no merge:** point `SymbolDatabase` at the project's own
    DB plus a separate read-only library DB via SQLite's `ATTACH DATABASE`, and
    extend queries (`findSymbolsByName`, `findSymbolsVisibleAt`, etc., §5.3) to
    `UNION` across both. Avoids the id-collision problem below entirely, since
    each attached DB's rows stay namespaced by schema name — but needs checking
    whether every existing query pattern (especially the hand-sorted-in-C++
    `UNION ALL` in `findSymbolsVisibleAt`, and the `file_id`-keyed foreign keys
    in `symbols`/`diagnostics`/`imports`/`instantiations`, `src/db/schema.h`)
    still works cleanly across two attached databases, and how project-side
    edits to a library file (should be rare/unsupported, but must not corrupt
    a shared, reused-by-other-projects DB) are handled.
  - **True merge into one DB:** copy the library DB's rows into the project's
    own DB. Harder, because every table's primary key (`files.id`, `symbols.id`,
    etc.) is a plain autoincrementing surrogate key with no cross-database
    uniqueness guarantee — a naive merge would collide ids and corrupt foreign
    keys; would need an id-remapping INSERT pass (or a schema change to a
    stable, content-derived key such as a path+hash, sidestepping remapping
    entirely — worth comparing against just keeping keys as-is and merging via
    `ATTACH` + `INSERT ... SELECT` with explicit id offsetting).
  - Either shape needs a **library-versioning/invalidation story**: how a
    project pins which pre-built library DB version it wants (a config field
    in `.svlsp.json`/`.svlsp.f`, §6.2?), how staleness is detected if the
    library's source changes without a corresponding rebuilt DB, and where
    pre-built DBs live/are distributed (built by whom, shipped how — this is
    genuinely unexplored, not just an implementation detail).
  - **Prior art confirmed (investigated 2026-08-28) — this is a solved problem
    in the language-tooling space, not something to design from scratch:**
    - **clangd's background index** validates the attach-and-query shape
      almost exactly: it persists one index shard per translation unit to an
      on-disk cache (`.cache/clangd/index`, with common-header shards like the
      STL cached under `$HOME/.cache/clangd/index` — i.e. already
      library/version-scoped, separate from any one project), and stitches
      multiple `SymbolIndex` instances together at query time via a
      `MergedIndex` that layers one index over another so every LSP feature
      sees one combined view — no physical merge, no shared key space, exactly
      the "keep them separate, `UNION` at query time" shape sketched above.
      Reference design doc: clangd.llvm.org/design/indexing.
    - **LSIF (Language Server Index Format) / its successor SCIP** validate
      both the "pre-built-once, reused-by-many-projects" premise *and* solve
      the id-collision problem the true-merge shape runs into here — LSIF
      v0.4.0 added support for dumping large systems project-by-project (in
      reverse dependency order) and combining the separate dumps into one
      database by **linking on stable "monikers"** (content/identity-derived
      cross-index symbol identifiers) rather than any per-dump surrogate key.
      This directly confirms the plan's own aside above: a stable,
      content-derived key (path+hash, or a moniker-style scheme) sidesteps the
      `files.id`/`symbols.id` autoincrement-collision problem entirely, rather
      than needing an id-remapping INSERT pass. (LSIF itself is now considered
      superseded by SCIP for new work, per Sourcegraph.)
    - Practical SQLite-level constraint worth remembering if the attach-based
      shape is pursued: `ATTACH DATABASE`'s default `SQLITE_MAX_ATTACHED` is
      10 databases per connection — fine for one project DB plus a handful of
      library DBs, but would need explicit handling (batching, or a higher
      compile-time limit) if a project ever wanted many separately-versioned
      library DBs attached at once.
    - **Conclusion for whoever picks this up:** don't invent the merge
      mechanism — model the design on clangd's per-shard `MergedIndex` (query
      time, no merge) as the default, and borrow LSIF/SCIP's moniker-style
      stable-key idea only if a true physical merge ever turns out to be
      necessary (e.g. for a single-file-DB distribution format). The
      still-open part is entirely this codebase's own integration surface:
      whether `SymbolDatabase`'s existing queries (`§5.3`) can be reshaped to
      query across an attached library DB cleanly, and the
      versioning/pinning/staleness story above, which has no external prior
      art to borrow — it's project-config design work specific to `svlsp`.
- **Not yet done:** grammar session 6's Fix 4 (`grammar/Sv.g4`, `class_property`'s
  `const` initializer, `constant_expression` → `expression`) is suspected (not
  profiled) to be why the full UVM corpus compile grew from session 5's
  ~14min/~1.3GB to ~16m47s/~6.7GB — the working theory per the handoff is that
  widening every `const` class property's initializer to the much larger,
  recursive `expression` rule triggers ANTLR's expensive full-context prediction
  fallback far more often corpus-wide, compounding with the pre-existing
  `data_type`/`variable_decl_assignment` ambiguity. Before narrowing Fix 4's
  scope (e.g. to a `constant_expression`-plus-`class_new` alternative instead of
  full `expression`), investigate what narrowing would actually give up: which
  real, non-`new(...)` UVM constructs (if any) rely on the full `expression`
  grammar for a `const` property initializer and would regress back to a parse
  error if narrowed. Confirm the performance theory with profiling (e.g. the
  SLL-only-mode technique from Phase 6.5's own session-2 finding) before
  deciding whether the narrowing is worth the risk.

### 6.6 Packaging
- A `make install` CMake target that places the `svlsp` binary and a sample `lsp-mode`
  Emacs snippet in a known location.
- A brief user guide in `docs/usage.md`.

### 6.7 Semantic Diagnostics / Lint Rules — user-extensible pattern rules

**Why this is needed:** today `publishDiagnostics` only ever carries ANTLR parse
errors, hardcoded to `DiagnosticSeverity::Error` (`src/lsp/diagnostics.cpp:39`) —
there is no way to flag semantically-valid-but-undesirable code (naming
conventions, deprecated API usage, project-specific gotchas) without writing new
C++/grammar code and rebuilding the server. Note also that the real `diagnostics`
table (schema v5, `src/db/schema.h`) has no `severity` column at all, despite
§5.1's original table sketch listing one — every diagnostic today is implicitly
Error. Teams applying svlsp to a real codebase (see `handoff.md`'s UVM-corpus
work) routinely want lightweight, project-specific checks (e.g. "flag any
`disable fork` without a preceding guard", "warn on direct use of an internal
UVM class outside `base/`") that they can add and iterate on themselves, as they
notice the need, without touching the grammar or recompiling `svlsp`.

**What is missing:**
- Schema migration `MIGRATION_V5_TO_V6`: add `severity TEXT NOT NULL DEFAULT
  'error'` and `source TEXT NOT NULL DEFAULT 'parser'` to `diagnostics`
  (`source = 'parser'` for existing ANTLR diagnostics, `'lint'` for rule-engine
  ones — lets the lint pass clear/refresh its own rows via `appendDiagnostics`
  without wiping a file's parser diagnostics, mirroring the existing
  `appendDiagnostics`/`replaceDiagnostics` split already used for the
  library-resolver's diagnostics, §5.3).
- `src/lsp/diagnostics.cpp`'s hardcoded `DiagnosticSeverity::Error` becomes a
  per-row lookup through a small `severityFor(string) -> lsp::DiagnosticSeverity`
  helper (`"error"→Error`, `"warning"→Warning`, `"info"→Information`,
  `"hint"→Hint`; an unrecognized value defaults to `Warning`, not `Error` — a
  malformed rule's severity typo shouldn't silently become build-breaking).
- New rule data model (`src/lint/lint_rule.h` — lives alongside the engine
  below, not `src/compiler/`, for the same "needs `lsp::json`, unavailable to
  `svlsp_compiler`" reason already documented in §6.2 Stage 3's "Key facts"):
  ```cpp
  struct LintRule {
      std::string id;             // stable identifier, e.g. "no-disable-fork"
      enum class Target { Text, Symbol } target;
      std::string pattern;        // regex — matched against a source line (Text)
                                   // or a SymbolRow field (Symbol)
      std::string symbolField;    // Symbol only: "name" | "detail" | "kind" | "scope"
      std::string symbolKind;     // Symbol only, optional: restrict to one
                                   // ParseRecordKind (e.g. "Class"); "" = any kind
      std::string severity;       // "error" | "warning" | "info" | "hint"
      std::string message;        // may reference regex capture groups: $1, $2, ...
  };
  ```
- **The actual user-facing "update warnings/errors by pattern" mechanism:** a
  plain JSON rules file, `.svlsp-lint.json`, discovered by `ProjectRegistry`
  using the same upward-search precedence list as `.svlsp.json`/`.svlsp.f`
  (§6.2 Stage 5) — a user edits and saves this file, no server rebuild and no
  grammar change required. Example:
  ```json
  {
    "rules": [
      {
        "id": "no-disable-fork",
        "target": "text",
        "pattern": "\\bdisable\\s+fork\\b",
        "severity": "warning",
        "message": "disable fork kills all descendant processes; prefer a named guard"
      },
      {
        "id": "class-naming-convention",
        "target": "symbol",
        "symbolKind": "Class",
        "symbolField": "name",
        "pattern": "^(?!uvm_)[a-z][a-z0-9_]*$",
        "severity": "info",
        "message": "class '$1' does not follow the project naming convention"
      }
    ]
  }
  ```
- `src/lint/lint_engine.h/.cpp`: `LintEngine::loadRules(path) -> vector<LintRule>`
  (JSON parse via `lsp::json`, same dependency `ProjectManifestParser` already
  uses). `LintEngine::evaluate(text, walkResult, rules) -> vector<LintDiagnostic>`
  (a small struct mirroring `ParseError{line, column, message}` plus the new
  `severity`/`ruleId` fields):
  - `Target::Text` rules: run the regex line-by-line over the original
    (pre-macro-expansion) source, translated through the existing preprocessor
    source map (§4.2a) — the same translation path `SvTreeWalker` already uses
    for parse errors, so a hit on a macro-expanded line still reports at the
    user's real file/line.
  - `Target::Symbol` rules: run the regex against each `ParseRecord`/`SymbolRow`
    field named by `symbolField`, filtered by `symbolKind` when set — reuses
    records `SvTreeWalker` already produces (§4.3/§4.4); no new parsing pass.
- `CompilationController::compile` (§5.4) gains a lint pass after the existing
  parse/extract step: load rules via `ProjectRegistry` (cached per discovered
  root the same way `ProjectConfig` is, §6.2 Stage 5 — re-parsing
  `.svlsp-lint.json` on every keystroke would be wasteful), run
  `LintEngine::evaluate`, and persist results via `appendDiagnostics` tagged
  `source = 'lint'` — parser diagnostics keep using `replaceDiagnostics` exactly
  as today, unaffected by this addition.
- A malformed rule (bad regex, unknown `target`/`symbolField`/`severity` value)
  must not crash the server or block compilation: `loadRules` skips just the
  offending rule, records one synthetic diagnostic on the rules file itself
  (`source = 'lint-engine'`, severity `error`) naming the bad rule's `id` and
  the problem, and continues evaluating the remaining valid rules.

**Integration tests (once the schema migration and engine exist):**
- A `.svlsp-lint.json` with one `Text` rule; a fixture containing the pattern
  gets a `Warning` diagnostic at the correct line, a fixture without it gets none.
- A `Symbol`/`Class`/`name` rule against a fixture with one conforming and one
  non-conforming class name; only the non-conforming one is flagged, at its
  declaration line.
- Three rules, one per non-error severity (`warning`/`info`/`hint`); verify
  lsp-mode reports each at its correct LSP severity, not all coerced to `Error`.
- A rules file with one valid rule and one rule with an invalid regex: the
  valid rule still fires, the invalid one produces exactly one engine-level
  diagnostic on the rules file, and the server neither crashes nor hangs.
- Edit `.svlsp-lint.json` on disk between two `didOpen`/`didChange` calls on the
  same SV file and verify (or explicitly decide and document, if not yet
  implemented for v1) whether the new pattern takes effect without a server
  restart — this exercises whatever cache-invalidation the
  `ProjectRegistry`-cached rule set needs, since `ProjectConfig` today
  (§6.2 Stage 5) is cached indefinitely once found with no invalidation on an
  external edit to the config file itself.

**Unit tests:** rule loading (valid, malformed, unknown `target`/`severity`);
`Text`-target matching against preprocessed source plus source-map translation;
`Symbol`-target matching against a `WalkResult`; severity string → enum mapping.

**Open question, deliberately unresolved:** whether `.svlsp-lint.json` rules
should be scoped one-file-per-project-root (as sketched above, matching every
other project-config file this codebase already has) or layerable (a repo-root
file plus optional per-subdirectory override/append files). Start with the
single-file model; revisit only if a real multi-team monorepo use case asks for
finer granularity.

### 6.8 Debounced / Asynchronous Compilation on `didChange` — Implemented (commit `d17855a`, 2026-08-28)

**Status:** implemented as designed below, with one deliberate deviation. Built:
`src/lsp/change_debouncer.h/.cpp` (`ChangeDebouncer` — the recommended one-thread,
`{key → deadline}` scheduler shape below, coalescing rapid `schedule()` calls per
key into one fire after a 300ms quiet period; `cancel()` for `didClose`); `didChange`
now updates `DocumentStore` immediately and calls `m_debouncer.schedule(uri)`
instead of compiling inline; a coarse `std::mutex m_dataMutex` in `LanguageServer`
guards `DocumentStore`/`Database`/`SymbolDatabase`/`CompilationController`/
`ProjectRegistry` across every handler, exactly the "minimum fix" described below.
**Deviation:** the framework's `lsp::AsyncNotificationResult`/`ThreadPool` support
described below turned out to be unnecessary — `didChange`'s handler already
returns immediately after `schedule()`, so `ChangeDebouncer`'s own worker thread
alone is sufficient to get the actual compile off the message-read thread; that
extra layer would have added complexity with no behavioral benefit here. All four
integration-test scenarios below are covered by
`tests/unit/lsp/test_server_debounce.cpp` (a full-stack test driving a real
`LanguageServer` over a real `Content-Length`-framed pipe transport, asserting
actual diagnostic *content* changes correctly across a debounced edit, not just
timing); the unit-test list below is covered by `tests/unit/lsp/
test_change_debouncer.cpp`. **Still open, unchanged:** the "Open question" at the
end of this section (fixed vs. configurable debounce interval) — not revisited,
per its own stated criterion ("only if real usage shows one value doesn't suit
both small and very large files").

**Why this is needed:** today `didChange` triggers a full, synchronous
`CompilationController::compile()` on the *same thread* that reads the LSP
transport (`src/lsp/server.cpp:72-79`, `MessageHandler::processIncomingMessages`
reads and processes exactly one message per call, `messagehandler.cpp:23-64`).
Since `textDocumentSync = Full`, every keystroke resends the whole document and
re-runs a full compile before the server reads its next message — including a
concurrent `hover`/`completion` request or the *next* `didChange`. Given the
documented per-file ANTLR parse cost (`handoff.md` — multi-second on large real
class bodies), fast typing can back the server up arbitrarily far behind the
editor, with no debouncing or coalescing today: each queued `didChange` is
compiled in full, one at a time, in arrival order.

**What is missing:**
- A debounce layer: on `didChange`, don't compile immediately — store the
  latest text/version for that URI and (re)start a short timer (~250-500ms,
  exact value TBD by feel-testing in Emacs). Only compile when the timer fires
  with no newer edit having arrived for that URI in the meantime. Standard
  pattern used by vscode/rust-analyzer/clangd; this codebase has no timer or
  event-loop facility today, so this needs a small purpose-built scheduler —
  not a per-keystroke thread. Recommended shape: one dedicated background
  thread owning a `{uri → {pendingText, pendingVersion, deadline}}` map,
  woken by a `std::condition_variable` either on a new `didChange` (reset the
  deadline) or when the nearest deadline elapses (compile, then remove the
  entry); `didClose` erases any pending entry for that URI.
- Getting the compile itself off the message-read thread. The framework
  already has the primitive for this: a notification handler may return
  `lsp::AsyncNotificationResult` (`= std::future<void>`), and when the
  incoming message isn't part of a batch (the normal case), the framework
  offloads waiting on that future to its own `ThreadPool` instead of blocking
  `processIncomingMessages()` (`third_party/lsp-framework/lsp/
  messagehandler.inl:113-142`). This alone is *necessary but not sufficient*:
  without the debounce layer above, it just moves per-keystroke compiles onto
  background threads without coalescing them, wasting work and still
  producing out-of-order diagnostic publishes.
- **Thread-safety, the real cost of this change.** Every shared piece of state
  is touched only from the single message-loop thread today and has zero
  locking: `DocumentStore` (`src/lsp/document_store.h`) is a bare `std::map`
  with no mutex; `m_symbolDb`/`m_db` are read by every position-based provider
  (hover/definition/completion) with no synchronization against a concurrent
  compile's writes. Moving compiles to a background thread makes
  `m_store.update()` (from the debounce worker) race against `m_store.get()`
  (from a hover/completion request on the main thread) — undefined behavior
  as the code is written today. Minimum fix: a mutex guarding `DocumentStore`
  access and one guarding `SymbolDatabase`/`Database` access, held across each
  full read or write (SQLite's own internal locking protects its process-wide
  state but not this codebase's higher-level assumption that a read observes
  a fully-compiled, self-consistent set of files).
- **Version correctness on publish.** After a debounced compile finishes,
  `publishDiagnostics` must carry the version that was actually compiled. With
  a "latest-edit-wins, older pending edit is simply replaced" debounce design
  this falls out naturally (there is only ever one pending version per URI),
  but it's worth an explicit test since a race here would silently show stale
  diagnostics as current.
- **What this does *not* fix:** debouncing reduces compile *frequency*, not
  per-compile *cost* — a multi-second compile on a large real file will still
  stall that file's diagnostics after the user pauses typing. That's the
  separate, already-tracked ANTLR parse-performance gap in §6.5; the two are
  complementary, not redundant.

**Integration tests:**
- Rapid-fire `didChange` (e.g. 10 changes within 100ms) on one document
  results in exactly one compile/`publishDiagnostics` for that document, not
  ten, and it reflects the *final* text.
- A `hover`/`completion` request sent while a debounced compile is pending (or
  running) for the same document still gets answered promptly from the
  previously-compiled DB state, rather than blocking behind the pending
  compile.
- `didClose` sent before a debounce timer fires results in no compile and no
  `publishDiagnostics` for that URI.
- Two different open documents each get their own independent debounce timer
  (editing one does not delay or coalesce with edits to the other).

**Unit tests:** debounce scheduler in isolation (reset-on-new-edit behavior,
fires once after the quiet period, per-URI independence, cancellation on
close) — should be testable without spinning up the real LSP transport, using
a fake clock or a short real interval plus generous test timeouts.

**Open question, deliberately unresolved:** whether the debounce interval
should be a fixed constant or configurable via `initializationOptions`
(mirroring how `explicitProjectConfigPath` is already threaded through, §6.2
Stage 5) — start with a fixed constant; revisit only if real usage shows one
value doesn't suit both small and very large files.

---

### 6.9 Keyword Completion

**Status:** implemented 2026-09-02, extended beyond this section's original
sketch: the user explicitly asked for **context-legality**, not just a flat
merged keyword list — illegal completions (their example: `module` offered
while already inside a module; SV design units cannot nest) must be
suppressed, not just ranked lower.

Implementation grounds legality in the one piece of position infrastructure
this codebase has: `SymbolDatabase::scopeAtPosition` (which
`findSymbolsVisibleAt` already relies on) reports the innermost enclosing
**declaration scope** at a cursor — `Module`/`Interface`/`Program`/`Package`/
`Class`/`Function`/`Task`, or `""` (top level). A new sibling,
`SymbolDatabase::scopeKindAtPosition` (`src/db/symbol_database.h/.cpp`), same
query shape, returns that scope's `kind` string instead of its name path.
`src/lsp/sv_keywords.h` holds the full ~254-entry keyword table (script-
extracted from every identifier-syntax literal in `grammar/Sv.g4` — see that
header's own comment for the exact extraction pattern), each tagged with a
`KeywordContext` bitmask over those 8 scope kinds. `CompletionProvider::
getCompletion` (`src/lsp/completion.cpp`) merges legal-for-this-scope keyword
candidates in with `findSymbolsVisibleAt`'s DB rows *before* the
`candidates.empty()` check (fixing this section's own flagged early-return
gap: an empty-DB file now still offers top-level keywords) and fuzzy-scores
both through one unified `Candidate{name, kind, detail}` pipeline. The
dot-completion branch (§6.10) is untouched — no keywords merge after an
explicit `.`, since there's no such thing as `foo.if`.

**Disclosed scope limit, deliberate:** this is *declaration-scope*
granularity, not statement/block granularity — nothing in this codebase
tracks "inside an `always_ff` block" vs "directly in the module body" (both
report scope kind `Module`, since `always`/`initial`/`begin`/`if` aren't
`ParseRecordKind`s). Legality is enforced at exactly the grain the user's own
example needs, not finer. A subtlety worth remembering for anyone extending
`sv_keywords.h`: an "end*" keyword (`endmodule`, `endclass`, `endfunction`,
`endtask`, `endinterface`, `endpackage`) is typed while the cursor is *still
inside* the body it closes, so it must be scoped to that body's own kind
(`KwModule`, `KwClass`, ...) — **not** to wherever the matching opener
(`module`, `class`, ...) is itself legal to type. Getting this backwards was
the exact bug caught by this feature's own unit tests during implementation
(`endmodule`/`endclass`/`endfunction`/`endtask` all needed fixing after an
initial pass bucketed openers and closers identically). Per-keyword context
assignment is grounded in `Sv.g4`'s own rule structure plus SV domain
knowledge, not a verified line-by-line LRM audit — same disclosed-uncertainty
posture as §6.13's built-in method lists — with a few genuinely uncertain
cases (`checker`/`endchecker` nesting) called out in the header's own
comments rather than asserted with false confidence. Where real legality
spans an unmodeled sub-context (e.g. `coverpoint`/`bins` are only legal
*inside* a covergroup body, which isn't its own tracked scope kind), the
keyword is bucketed with its nearest enclosing trackable context rather than
dropped — false positives are the accepted failure mode, never false
negatives.

Tests: `tests/unit/lsp/test_sv_keywords.cpp` (table sanity + representative
context-bucket spot-checks), new `CompletionProvider` cases in
`tests/unit/lsp/test_completion.cpp` covering top-level/Module/Class/Function
scope legality (including the "can't nest modules" case) plus a
`scopeKindAtPosition` unit in `tests/unit/db/test_symbol_database.cpp`;
functional coverage in `tests/integration/test_28_keyword_completion.sh` +
fixture `tests/integration/fixtures/keyword_completion.sv`.

Original sketch (superseded by the above, kept for history):

**Why this is needed:** `CompletionProvider::getCompletion`
(`src/lsp/completion.cpp`) only ever ranks rows from
`db.findSymbolsVisibleAt(path, line1)` — declared symbols (modules, classes,
signals, etc.). SystemVerilog reserved words (`always_ff`, `logic`, `endmodule`,
`interface`, `foreach`, ...) never appear as completion candidates today, even
where one is the only thing that can syntactically go next (e.g. right after a
newline inside a module body, or after `always_`). §3.6's original scope
("keywords, module ports, signal names, macros") already named this; it was
never implemented and dropped off the visible gap list once fuzzy-matching
symbols shipped.

**What is missing:**
- **A keyword list.** The grammar (`grammar/Sv.g4`) has no centralized lexer
  token for keywords — they're inline string literals scattered across parser
  rules (e.g. `'module'` at `Sv.g4:69`, `'always'` at `Sv.g4:2053`), so there is
  no single rule to enumerate. Two options: (a) hand-maintain a list from the
  LRM (IEEE 1800-2017 Annex B, "Keywords"), or (b) script-extract every quoted
  literal from `grammar/Sv.g4` matching identifier syntax (`'[a-z_][a-z0-9_]*'`)
  and dedupe — cheaper to keep in sync as the grammar evolves, but would need a
  one-time pass to hand-filter any accidental non-keyword matches. Store as a
  `static const` list (e.g. `src/lsp/sv_keywords.h`), not derived at runtime.
- **Context-blind vs. context-aware surfacing.** Simplest correct starting
  point: always include the keyword list as additional candidates in
  `getCompletion`, fuzzy-scored against the prefix exactly like symbol rows
  today (same `fuzzyScore` call, same sort) — a keyword is a candidate
  wherever an identifier-like token is being typed, same as any symbol.
  **Not pursuing real syntactic position-awareness** (e.g. suppressing
  `endmodule` inside an expression) in the first cut — that needs parser
  state at the cursor, which nothing in `SvTreeWalker`/`ParseRecord` provides
  today; matches this codebase's existing "text/fuzzy, not semantic" posture
  for completion.
- **The `rows.empty()` early return.** `getCompletion` currently does
  `if (rows.empty()) return nullptr;` before any scoring — a file with zero
  visible DB symbols (e.g. an empty new buffer) returns no completions at
  all today. Once keywords are a candidate source independent of
  `findSymbolsVisibleAt`, this early return must move past the point where
  keyword rows are merged in, or keyword completions will wrongly disappear
  exactly when a mostly-empty file needs them most.
- **Completion item kind.** LSP has `CompletionItemKind.Keyword` (14) —
  `completionKindFor` (`src/lsp/symbol_utils.cpp`) maps DB kind strings to
  `CompletionItemKind` today; keyword rows aren't DB rows, so they need their
  own direct `item.kind = lsp::CompletionItemKind::Keyword` assignment rather
  than going through that DB-kind mapping function.

**Functional test:** trigger completion at a position where only a keyword
plausibly makes sense (e.g. start of a line inside a module body, or a
partial like `alw` triggering `always`/`always_comb`/`always_ff`/`always_latch`);
verify the keyword appears in the Emacs completion list alongside any
symbol matches.

**Unit tests:** keyword list is present in results with no typed prefix
(merged with symbol rows, not replacing them); a keyword-matching prefix
(`alw`) ranks and returns the right keyword candidates via the existing
fuzzy scorer; a file with zero DB symbols still returns keyword completions
(regression test for the `rows.empty()` early-return fix above).

**Open question, deliberately unresolved:** whether to ship the full LRM
Annex B keyword set as-is, or trim to commonly-typed ones (there are a few
hundred, including many rarely-used verification/assertion keywords) — start
with the full set from whichever source (Annex B or grammar-extracted) is
chosen above; only trim if real usage shows the completion list feels noisy.

---

### 6.10 Dot / Member-Access Completion (`foo.bar`)

**Status:** implemented 2026-09-01. Single-segment case only, as recommended
below. Two scoped deviations from this section's original sketch, both
documented in code:
- `detail`-population only covers `Signal` and `Parameter`, not `Port` —
  `Port`'s `detail` was already in use for port direction (see
  `ParseRecord::detail`'s doc comment and `enterAnsi_port_declaration`), and
  overloading it with type text too would have silently broken existing
  Port-hover behavior (`test_sv_listener.cpp`/`test_document_symbols.cpp`
  hardcode `"input"` as a Port's `detail`). Dot-completion on a class/
  interface-typed port isn't supported by this first cut.
- `enterNet_declaration` also needed the same `userTypeName()` treatment,
  not just `enterData_declaration` as originally sketched: a bare
  `MyClass foo;` turned out to be grammatically ambiguous between
  `data_declaration` (MyClass classified as a `data_type`'s
  `type_identifier`) and `net_declaration`'s own alt 2 (MyClass classified
  as a `net_type_identifier` — a user-defined nettype) — this codebase's
  grammar resolves that case via the latter, not the former, so
  `enterData_declaration` alone never saw it. A second, distinct
  manifestation of the same "identifier classification needs a symbol
  table" problem already documented for `data_type`'s own alternatives
  (grammar quirks table, `handoff.md`) — not previously noticed because
  nothing depended on which alt fired until now.

**Why this is needed:** `CompletionProvider::getCompletion` always scores
candidates from `db.findSymbolsVisibleAt(path, line1)` — every symbol visible
by lexical scope at that line, regardless of what was typed before the
cursor. When completion is triggered right after a `.` (e.g. typing `foo.` or
`foo.ba`), the only sensible candidates are `foo`'s own members (its class's
methods/properties, an interface's signals, a struct's fields) — not every
symbol visible in the enclosing scope. Today a `.` gets no special handling
at all: `wordAtPosition` (`src/lsp/symbol_utils.cpp`) treats `.` as a
non-identifier boundary and simply stops there, so the object being
dereferenced is silently dropped and the candidate set stays scope-wide
instead of narrowing to the object's type.

**What is missing:**
- **Detecting a dot-completion context.** `wordAtPosition` walks left from
  the cursor only over identifier characters (`isId`: alnum/`_`/`$`); it
  needs a sibling helper (or an extension returning both the typed prefix
  *and* whatever identifier chain precedes an immediately-preceding `.`) that
  additionally walks left across `.` to recover the object expression, e.g.
  for `foo.b|` (cursor at `|`) yield `{object: "foo", prefix: "b"}`, and for
  `foo.|` yield `{object: "foo", prefix: ""}`. Only the single-segment case
  (`foo.bar`) is in scope for a first cut; chained access (`foo.bar.baz`) is
  explicitly deferred (see open question below).
- **Resolving the object's declared type.** This is the real gap, not the
  parsing above: nothing in the schema captures a variable's declared type
  today. `enterData_declaration` (`src/compiler/sv_tree_walker.cpp:241-250`)
  emits a `Signal` `ParseRecord` with an empty `detail` — the `MyClass` in
  `MyClass foo;` is parsed but never recorded anywhere. `detail` is
  otherwise used for exactly this kind of kind-specific payload (port
  direction, class parent, return type — see `ParseRecord::detail` in
  `src/compiler/parse_record.h`), so the natural fix is populating it with
  the declared type text for `Signal`/`Port`/`Parameter` records whose
  declaration is a named user type (class/interface/struct/typedef), leaving
  it empty for built-in types (`logic`, `int`, ...) where there's no member
  scope to resolve into. Schema/`SymbolRow` need no changes — `detail`
  already round-trips through `symbols.detail`.
- **Looking up the type's member scope.** Once the object's declared type
  name is known, `SymbolDatabase::findSymbolsInScope(scope)`
  (`src/db/symbol_database.cpp:336`) already does exact-scope member lookup
  — this is exactly what backs today's non-dot completion's local-scope arm,
  so no new query is needed, only a new call site: resolve `foo`'s `detail`
  by looking `foo` itself up (`findSymbolsByName`/`findSymbolsVisibleAt`
  scoped search for the identifier before the dot), then
  `findSymbolsInScope(thatDetailValue)` instead of
  `findSymbolsVisibleAt(path, line1)` for the member list.
- **`CompletionProvider::getCompletion` branch.** Add the dot-context check
  before building the candidate set: if `wordAtPosition`'s extended helper
  reports an object expression, resolve it to member rows as above and
  fuzzy-score only those against `prefix`; otherwise fall through to
  today's `findSymbolsVisibleAt` behavior unchanged. An object that fails to
  resolve (undeclared variable, built-in type, unresolved cross-file type)
  should return `nullptr`/empty rather than silently falling back to
  scope-wide completion — offering unrelated symbols after an explicit `.`
  would be a worse result than offering nothing.

**Functional test:** declare `MyClass foo;` with `MyClass` having a known
method/property; trigger completion at `foo.` and at `foo.<partial-prefix>`;
verify only `MyClass` members appear (Emacs). Negative case: completion after
`.` on a built-in-typed variable (e.g. `int x; x.`) returns no completions,
not the whole visible scope.

**Unit tests:** the dot-detection helper on `foo.|`, `foo.b|`, and (for
contrast) `foo|` / bare `.` with no preceding identifier; `detail`
population for a class-typed `Signal`/`Port` declaration; end-to-end
`getCompletion` returning exactly the target class's member rows for a
`foo.` request, scored/sorted the same way as the existing prefix path.

**Open question, deliberately unresolved:** whether to support chained
member access (`foo.bar.baz`, where `bar`'s own type must itself be resolved
to reach `baz`'s scope) in this pass or defer it — start with the
single-segment case (`foo.bar`) as recommended above, since it already
requires the new `detail`-population work end to end; extend to chains only
if real usage shows single-segment dot completion isn't enough. Also
unresolved: whether array/queue-typed objects (`foo[0].bar`) and `this.`/
`super.` inside a class body are treated specially or naturally fall out of
the same object-identifier resolution (`this`/`super` aren't ordinary
declared variables, so `findSymbolsByName` won't find them as-is) —
worth a quick scoped follow-up once the base case above lands, not blocking
it.

---

### 6.11 Configurable Fuzzy-Matching Toggle

**Status:** implemented 2026-09-04, following this section's own original
sketch essentially as designed (the parameter-threading option was taken
over the non-static-class alternative, exactly as recommended below).

**As implemented:** `ServerState` (`src/lsp/server_state.h/.cpp`) gained
`fuzzyCompletionEnabled()`, mirroring `explicitProjectConfigPath()` exactly:
a private `extractFuzzyCompletionEnabled(params)` static helper reads
`initializationOptions.svlsp.fuzzyCompletion`, defaulting to `true` (fuzzy
matching stays on, today's behavior unchanged) whenever
`initializationOptions` is absent, isn't an object, the nested `svlsp` key is
missing/not an object, or `fuzzyCompletion` is missing/not a JSON boolean —
resolved once in `handleInitialize` and fixed for the server's lifetime, the
same "no live reconfiguration" posture `explicitProjectConfigPath` already
has. `CompletionProvider::getCompletion` (`src/lsp/completion.h/.cpp`) gained
a `bool fuzzyEnabled = true` parameter (default preserves every pre-existing
call site and unit test unchanged); the server's own
`textDocument/completion` handler (`src/lsp/server.cpp`) passes
`m_state.fuzzyCompletionEnabled()` through explicitly.

The disabled path lives entirely inside the shared `buildCompletionItems`
helper (`src/lsp/completion.cpp`) — both the dot-completion branch and the
plain-scope branch call through the same function, so neither needed its own
copy of this logic. With `fuzzyEnabled=false` and a non-empty prefix, a
candidate is kept only via a strict, case-sensitive
`compare(0, prefix.size(), prefix) != 0` prefix rejection (matching §3.6's
own documented description of the pre-fuzzy state) instead of `fuzzyScore`;
the `std::sort` re-rank is skipped entirely (candidate/DB order preserved,
not re-derived); no `sortText` is assigned. An empty prefix is unaffected
either way (every candidate stays in, unranked, matching current behavior
regardless of the flag) — this was already true before this section and
needed no change.

**A subtlety the original sketch didn't anticipate, found while writing the
disabled-mode unit tests:** for any candidate set that actually *survives* a
strict-prefix filter, fuzzy-enabled ranking degenerates to the exact same
relative order strict/disabled mode already preserves. A full literal-prefix
match scores identically for every surviving candidate (same word-boundary
and contiguous-run bonuses, since the matched span is identical), so
`buildCompletionItems`'s own score-tie tie-break (candidate name, ascending)
is the only thing distinguishing them when fuzzy is on — and that happens to
coincide with plain alphabetical order for candidates sharing the same scope
depth. Proving disabled mode truly preserves *DB* order (not just "an order
indistinguishable from fuzzy's tie-break") therefore needed a candidate pair
at two different scope depths, since `findSymbolsVisibleAt`'s own SQL-side
sort (deepest scope first, name second) and fuzzy's flat
score-then-name-only sort genuinely disagree once scope depth is allowed to
differ — see `tests/unit/lsp/test_completion.cpp`'s
"fuzzyEnabled=false preserves DB order, not fuzzy tie-break order" test for
the worked example.

**Verification:** unit tests extend both `tests/unit/lsp/test_server_state.cpp`
(the same present/absent/wrong-type/non-object matrix `explicitProjectConfigPath`
already has, for `true`/`false`/default) and `tests/unit/lsp/test_completion.cpp`
(a typo'd/skip-tolerant prefix that fuzzy-matches WIDTH is rejected outright
when disabled; a strict-prefix match still succeeds with no `sortText`
attached; the DB-vs-tie-break order distinction above; and that omitting the
parameter is byte-for-byte identical to passing `true` explicitly).
Functional test `tests/integration/test_35_fuzzy_completion_toggle.sh`
reuses the `fuzzy_completion.sv` fixture test_25 already established, proving
the flag reaches `CompletionProvider` through a real `initialize` handshake,
not just at the unit level — the one real complication: every fixture in
this repo shares one lsp-mode workspace/server process (see test_21's own
header comment on this), so observing the flag's effect requires actually
tearing that shared workspace down and reconnecting with new
`initializationOptions`. `lsp-workspace-restart` was tried first and found
unreliable here (its respawn is driven asynchronously off the *dying*
workspace's own buffer list via the process sentinel, and a
`textDocument/completion` request sent immediately after intermittently
raced a not-yet-fully-reattached buffer, timing out); the test instead does
an explicit `lsp-workspace-shutdown` (`lsp-restart` is set to `ignore` in
`emacs-test-init.el`, so nothing auto-respawns it), polls for the killed
process to actually die before proceeding (`lsp-workspace-shutdown` only
kills the process — the session's `folder->servers` table isn't cleaned up
until the process sentinel fires asynchronously on process death, so
skipping this wait let a fresh buffer open race that cleanup and reattach to
the dying workspace instead of spawning a new one), then opens a *fresh*
buffer of the same fixture — the same "no workspace yet for this root"
connect path every other test in this suite already relies on. The test
file's own last step unconditionally resets the option to absent and
reconnects again, restoring the shared workspace to its default (fuzzy-on)
state for every test that runs after it, regardless of whether the earlier
assertions in the same file passed.

**Original sketch, kept below for history:**

**Why this is needed:** `CompletionProvider::getCompletion`
(`src/lsp/completion.cpp`) unconditionally fuzzy-scores every candidate via
`fuzzyScore` when a prefix is typed (§3.6). This is a good default, but it's
a real behavior change from strict-prefix matching (skip-tolerant, reordered
matches never match but non-contiguous ones do) that not every user/editor
setup wants — e.g. someone relying on the exact-prefix ordering for muscle
memory, or wanting deterministic results for scripted/automated completion
requests. There's no way to turn it off today short of editing source.
This should apply uniformly to whatever candidate set completion is scoring
against, including keyword rows (§6.9) and dot/member rows (§6.10) once
those land, since both are proposed to reuse the same `fuzzyScore`/sort call.

**What is missing:**
- **A new `initializationOptions.svlsp.fuzzyCompletion` boolean**, following
  the exact pattern `ServerState::extractProjectConfigPath`
  (`src/lsp/server_state.cpp:60-70`) already establishes for
  `svlsp.projectConfig`: absent/non-object/wrong-type all mean "use the
  default" (fuzzy **on**, preserving today's behavior unchanged) — no client
  needs to opt in to get current behavior. `ServerState` gains a
  `fuzzyCompletionEnabled()` accessor mirroring
  `explicitProjectConfigPath()` (`src/lsp/server_state.h:46-50`), set once
  in `handleInitialize`, fixed for the server's lifetime — same "resolved at
  `initialize`, not live-reconfigurable" posture as the project-config path,
  for the same reason (no `workspace/didChangeConfiguration` handling exists
  in this codebase today).
- **Threading the flag into `CompletionProvider::getCompletion`.**
  `getCompletion` is currently a stateless `static` method
  (`src/lsp/completion.h`) taking `(params, db, docText)` with no server
  reference; it needs either a new `bool fuzzyEnabled` parameter (simplest,
  keeps it stateless — `LanguageServer` reads
  `m_state.fuzzyCompletionEnabled()` at the `textDocument/completion`
  handler call site and passes it through) or the class becomes
  non-static/constructed with the flag. Prefer the parameter — matches this
  codebase's existing "pass config in, don't stash server-wide flags on
  providers" style (`CompilationController::compile`'s `config` parameter is
  the closest precedent).
- **Disabled behavior.** With the flag off, restore exactly the
  pre-fuzzy-matching semantics §3.6 describes as the prior state: strict
  prefix filter (`row.name.compare(0, prefix.size(), prefix) != 0` to
  reject), DB order preserved, no `sortText` assigned, no ranking — i.e.
  skip the `fuzzyScore` call and the `std::sort` entirely rather than
  special-casing fuzzy score `0` as "off" (a real fuzzy match can legitimately
  score low; that's not the same thing as strict-prefix-only).

**Functional test:** two Emacs runs (or two requests against the same
running server via two different `initialize` calls) with
`fuzzyCompletion: true` vs `false`/absent — a misspelled/non-contiguous
prefix that fuzzy-matches a symbol should be present in the `true` case and
absent in the `false` case.

**Unit tests:** `ServerState::extractFuzzyCompletionEnabled`-equivalent
parsing (present-true, present-false, absent, non-object, wrong-type — all
mirroring the existing `extractProjectConfigPath` test matrix); `getCompletion`
with the flag off returns strict-prefix-only, DB-ordered, un-ranked results
for a non-contiguous prefix that would otherwise fuzzy-match; with the flag
on (or omitted), behavior is byte-for-byte unchanged from today's tests.

**Open question, deliberately unresolved:** whether this ever needs to be
per-request rather than server-wide/fixed-at-`initialize` (e.g. a client
exposing its own "fuzzy matching" editor setting that can flip mid-session)
— start with the fixed-at-`initialize` model above, consistent with how
`explicitProjectConfigPath` already works and how §6.8's debounce interval
is deliberately *not* made configurable yet (§6.12 proposes changing that);
revisit only if a real editor integration needs it to change without
restarting the server.

---

### 6.12 Configurable Debounce Interval

**Status:** not started. Resolves the open question §6.8 deliberately left
unresolved ("start with a fixed constant; revisit only if real usage shows
one value doesn't suit both small and very large files") and item #10/§6.8's
"Remaining open point" in `handoff.md`.

**Why this is needed:** `ChangeDebouncer`'s 300ms quiet-period is currently a
hardcoded default (`std::chrono::milliseconds delay =
std::chrono::milliseconds{300}` at `src/lsp/change_debouncer.h:26-27`), and
`LanguageServer` never passes an explicit value, so every project gets the
same interval regardless of file size or machine speed. §6.5's documented
per-file ANTLR parse cost (multi-second on large real class bodies) means
300ms may fire a debounced compile that's still nowhere near done before the
*next* keystroke's compile is queued behind it on very large files, while a
tiny file might tolerate (and a fast typist might prefer) a shorter delay.
This should be a value users/editors can tune per-project rather than a
constant baked into the binary.

**What is missing:**
- **A new `initializationOptions.svlsp.debounceMs` integer**, same
  extraction pattern as `svlsp.projectConfig`
  (`ServerState::extractProjectConfigPath`, `src/lsp/server_state.cpp:60-70`)
  and the proposed `svlsp.fuzzyCompletion` in §6.11: absent/non-object/
  wrong-type/non-positive all mean "use the existing 300ms default." Add a
  `ServerState::debounceInterval()` accessor (or similar) returning
  `std::chrono::milliseconds`, mirroring `explicitProjectConfigPath()`.
- **The construction-order wrinkle, the real blocker here.** Unlike §6.11's
  flag (only ever read at the moment a `textDocument/completion` request is
  handled, well after `initialize`), `m_debouncer` is a `LanguageServer`
  member constructed **in the constructor's member-initializer list**
  (`src/lsp/server.cpp:15-17`), which runs at process startup — before the
  client has sent `initialize` and before `initializationOptions` exist at
  all. `ChangeDebouncer`'s constructor can't be handed a value that doesn't
  exist yet. Two ways through:
  (a) give `ChangeDebouncer` a `setDelay(std::chrono::milliseconds)` mutator
      called from the `initialize` handler once `m_state.debounceInterval()`
      is available, applied to future `schedule()` deadlines only (not
      retroactively to whatever's already pending); requires guarding
      `m_delay` with the existing `m_mutex` (`src/lsp/change_debouncer.h:44,
      46`) since it's currently read unsynchronized in the worker's `run()`
      loop, safe only because it's never mutated post-construction today; or
  (b) keep `ChangeDebouncer` immutable but stop constructing it in
      `LanguageServer`'s member-initializer list — switch it to a
      `std::optional<ChangeDebouncer>` or heap-owned member, constructed
      lazily inside `handleInitialize`'s call path once the interval is
      known. Larger structural change (every use site would need a
      null/not-yet-constructed check for the brief window between process
      start and `initialize`, though in practice `didChange` can't arrive
      before `initialize` per the LSP spec's own ordering guarantee, so that
      window is inert).
  Prefer (a): smaller diff, keeps `ChangeDebouncer` unconditionally
  constructed (matching every other `LanguageServer` member today), and the
  mutex it already has makes a synchronized setter cheap.
- **Validation.** A client-supplied `debounceMs` of `0` or negative is
  nonsensical (0 would mean "never coalesce," defeating §6.8 entirely) —
  clamp to the existing 300ms default rather than accepting it literally, and
  note this in whatever docs eventually cover `initializationOptions.svlsp.*`
  (none exist yet as of this writing — first configurable option was
  `projectConfig`, this would be the third alongside §6.11's flag).

**Functional test:** start the server with `debounceMs: 50` and with the
default (absent); type a burst of edits into an Emacs buffer in both cases
and confirm the shorter interval visibly reduces time-to-diagnostics-update
without breaking the existing single-publish-per-burst guarantee from
§6.8's integration tests.

**Unit tests:** `ServerState` extraction matrix for `debounceMs` (present
valid, present zero/negative, absent, non-object, wrong-type); `ChangeDebouncer::setDelay`
changes the interval used for *subsequently scheduled* keys without
disturbing an already-pending deadline for a key scheduled before the call
(extends `tests/unit/lsp/test_change_debouncer.cpp`); a `LanguageServer`-level
test (extending `tests/unit/lsp/test_server_debounce.cpp`) confirming a
custom `initializationOptions.svlsp.debounceMs` actually changes observed
fire timing end-to-end.

**Open question, deliberately unresolved:** what the effective interval
should default to, or whether it should scale automatically with file size
instead of being a single flat value — out of scope here; this section only
makes the *existing* fixed 300ms into a configurable-but-still-flat value,
per §6.8's own stated resolution criterion.

---

### 6.13 Built-in Container & Randomization Method Completion (queues, associative arrays, mailboxes, `randomize`)

**Status:** implemented 2026-09-02, expanded well beyond this section's
original scope after the user rejected a first-pass plan as
under-researched (naming `process` as a concrete miss). The shipped set,
closed by that re-research: queues, associative arrays, dynamic arrays,
fixed-size unpacked arrays, `mailbox`, `semaphore`, `process`, `string`,
`event` (`.triggered`), and the randomize-family methods every class
implicitly gains.

Every claim below was verified empirically by piping real LSP requests
into `build/debug/svlsp` (the smoke-test pattern earlier in this doc), not
just inferred from the grammar — reading the grammar alone was previously
shown unreliable for this exact question (see the `data_type`/
`net_declaration` ambiguity in "Sv.g4 grammar quirks" in `handoff.md`).
Findings that changed the plan: (1) `int arr[];` (a dynamic array) is
**already** recorded as a `Signal` today — the grammar's alternate
`dynamic_array_variable_identifier` production that looked like it should
fire does not; ANTLR resolves it via the ordinary
`variable_identifier variable_dimension*` alternative instead, so no
tree-walker bug existed, only a missing tag; (2) `process` and `semaphore`
already get a correct `detail` via `userTypeName()`'s existing `class_type`
path today (neither is a grammar keyword), so — like `mailbox` — they
needed zero tree-walker changes, only a lookup-table entry keyed by literal
type name; (3) `string`/`event` are bare literal `data_type` alternatives
with no `type_identifier()`/`class_type()` accessor, so a **separate**
detection helper (`builtinBareTypeTag`, checking `data_type()->getText()`)
was needed rather than extending `userTypeName()`; (4) fixed-size unpacked
arrays (`int arr[8]`) get the LRM §7.12 locator/ordering/reduction methods
too, via the exact same per-declarator dimension scan already needed for
queues/associative arrays — in scope now, left out of the original sketch
only by oversight, not a deliberate cut.

**Deliberately still out of scope:** covergroup built-in methods
(`cg.sample()`, `cg.get_coverage()`, ...) — researched and confirmed that
covergroups aren't tracked as any `ParseRecordKind` today at all (no
`enterCovergroup_declaration` exists), so supporting this would need a new
`ParseRecordKind` and tree-walker listener, disproportionate to a
"dot-completion on built-ins" pass. Left as a follow-up.

See `src/lsp/sv_builtin_methods.h` for the full method tables (`QUEUE_METHODS`,
`ASSOC_ARRAY_METHODS`, `DYNAMIC_ARRAY_METHODS`, `FIXED_ARRAY_METHODS`,
`MAILBOX_METHODS`, `SEMAPHORE_METHODS`, `PROCESS_METHODS`, `STRING_METHODS`,
`EVENT_MEMBERS`, `RANDOMIZE_METHODS`) and `builtinMethodsFor()`'s single
dispatch point; `src/compiler/parse_record.h` for the five `$`-prefixed
detail-tag constants (`CONTAINER_QUEUE`/`CONTAINER_ASSOC`/
`CONTAINER_DYNAMIC_ARRAY`/`CONTAINER_FIXED_ARRAY`/`CONTAINER_STRING`/
`CONTAINER_EVENT`); `src/compiler/sv_tree_walker.cpp`'s
`enterData_declaration` for the per-declarator tagging (container dimension
wins over the bare-type tag, which wins over the ordinary class/interface
type name — e.g. `string s[$]` is tagged as a queue, not a string, since
the container's own methods are what matter for a queue-of-strings).
`randomize`-family methods are unioned onto a resolved object only when a
`Class`-kind DB row actually exists for its type name (gated separately
from the container/literal-name dispatch, in `completion.cpp`) — an
unresolved/bogus type keeps failing closed rather than surfacing
`randomize()` for a made-up name, and a class overriding `randomize` itself
keeps the real DB entry rather than duplicating it.

**Disclosed uncertainty, unchanged:** every method name/signature in the
tables above is reconstructed from training-time familiarity with IEEE
1800-2017, not verified against an LRM copy (this server has no access to
one) — same posture as this section's original text and §6.9's
`checker`-nesting caveat.

Tests: `tests/unit/compiler/test_sv_listener.cpp` (one tagging case per
built-in kind, plus the shared-declarator independence case `int a[$], b;`);
`tests/unit/lsp/test_sv_builtin_methods.cpp` (table sanity); new
`CompletionProvider` cases in `tests/unit/lsp/test_completion.cpp` (one
dot-completion case per kind, the randomize-union case, the
own-`randomize`-wins collision case, an unresolved-type fail-closed
regression); functional coverage in
`tests/integration/test_29_builtin_method_completion.sh` + fixture
`tests/integration/fixtures/builtin_method_completion.sv` (a representative
subset: queue, associative array, mailbox, process, plain class).

Original sketch (superseded by the above, kept for history):

**Status (original):** not started. Extends §6.10 (dot/member-access completion,
implemented 2026-09-01) — depends on it, doesn't replace any of it.

**Why this is needed:** §6.10's dot-completion only ever resolves an object
to a DB-declared `Class`/`Interface` scope via `findSymbolsInScope`. Two
whole categories of real-world dot-completion targets are still
unsupported: (1) SystemVerilog's built-in container types — queues
(`T q[$]`), associative arrays (`T aa[K]`), and the built-in
`mailbox`/`mailbox #(T)` class — none of which have a `Class`-kind
`ParseRecord` in the DB (queues/associative arrays aren't classes at all;
`mailbox`'s own member functions are specified by the LRM, never parsed
from any source file this server sees), so `foo.` on any of these returns
nothing today; and (2) every user-declared class implicitly gains a set of
built-in randomization methods (`randomize`, `pre_randomize`, ...) the LRM
grants automatically, which never appear as a `ParseRecord` since nothing
in user source declares them — `foo.` on an ordinary class instance today
only ever shows what the user explicitly wrote in the class body.

**Native method lists — researched from recollection of IEEE 1800-2017;
cross-check exact names/signatures/section numbers against an actual LRM
copy before shipping user-facing completion text, this server has no
access to the (copyrighted) spec to verify against directly:**

- **Queue methods** — §7.10.2 (queue-specific) plus §7.12 (array locator/
  ordering/reduction methods, which a queue also supports as a
  variable-size ordered collection):
  - `size()`
  - `insert(index, item)`
  - `delete([index])` — `delete()` with no argument empties the whole queue
  - `pop_front()`, `pop_back()`
  - `push_front(item)`, `push_back(item)`
  - Locator methods (§7.12.1, each also accepts an optional `with (expr)`
    clause): `find()`, `find_index()`, `find_first()`, `find_first_index()`,
    `find_last()`, `find_last_index()`, `min()`, `max()`, `unique()`,
    `unique_index()`
  - Ordering methods (§7.12.2): `reverse()`, `sort()`, `rsort()`,
    `shuffle()`
  - Reduction methods (§7.12.3): `sum()`, `product()`, `and()`, `or()`,
    `xor()`
- **Associative array methods** — §7.8.3, plus the §7.12.3 reduction
  methods only (**not** the locator/ordering ones — an associative array's
  index isn't linearly ordered unless the index type itself is, so
  `sort`/`min`/`max`/etc. don't carry over the way they do for queues; do
  not reuse the queue list wholesale):
  - `num()`
  - `delete([index])` — `delete()` with no argument empties the whole array
  - `exists(index)`
  - `first(ref index)`, `last(ref index)`, `next(ref index)`,
    `prev(ref index)`
  - `sum()`, `product()`, `and()`, `or()`, `xor()`
- **`mailbox`/`mailbox #(T)` methods** — §15.4 (`mailbox` is itself
  specified as a built-in parameterized class):
  - `new([bound])`
  - `put(message)` *(task)*, `try_put(message)` *(function, returns `int`)*
  - `get(ref message)` *(task)*, `try_get(ref message)` *(function, returns
    `int`)*
  - `peek(ref message)` *(task)*, `try_peek(ref message)` *(function,
    returns `int`)*
  - `num()`
- **Randomization-family methods, implicitly present on every class** —
  §18.6–§18.8:
  - `randomize()` — also usable as `randomize() with { constraints }`, a
    distinct grammar construct from a plain method call; this section only
    covers plain-call completion of the method name itself, not
    `with`-clause-aware completion (see open questions)
  - `pre_randomize()`, `post_randomize()` — user-overridable hooks, called
    automatically by `randomize()`
  - `srandom(seed)`
  - `get_randstate()`, `set_randstate(state)`
  - `rand_mode(on_off)`, `constraint_mode(on_off)`

**What is missing — detection & wiring:**
- **Recognizing a queue/associative-array-typed declaration.** Unlike
  §6.10's `userTypeName()` (which reads the *base* `data_type`), a
  queue/associative array is signaled by the *dimension* suffix on the
  specific declarator, not the base type — `int q[$]` is `data_type=int`,
  and the `[$]` lives on `variable_decl_assignment`'s own
  `variable_dimension()` list (`grammar/Sv.g4:1055-1064`;
  `Variable_dimensionContext` exposes `queue_dimension()`/
  `associative_dimension()`/`unsized_dimension()`/`unpacked_dimension()` as
  direct, individually-checkable accessors). So `int a[$], b;` must record
  `a` as a queue and `b` as a plain `int` even though they share one
  `data_type` — detection has to happen per-`vda` inside
  `enterData_declaration`'s existing loop (`src/compiler/sv_tree_walker.cpp`),
  not on the shared `data_type_or_implicit`.
- **Recognizing `mailbox`/`mailbox #(T)`.** No new grammar traversal
  needed — `userTypeName()` already extracts `"mailbox"` as plain text via
  its existing `class_type` alt (mailbox is parsed as an ordinary
  parameterized class-type reference); detection is just a literal-name
  check (`typeName == "mailbox"`) after the fact. `T` itself (the queued
  message type) isn't needed for method-name completion, only for
  hovering/typing the arguments, which is out of scope here.
- **A `ParseRecord::detail` tagging convention distinct from a real type
  name.** `findSymbolsInScope(scope)` only ever answers "what's declared
  with `scope == this class name" — there is no DB scope named "queue" or
  "mailbox" to look up. Proposed: reserve a `detail` value starting with
  `$` (e.g. `"$queue"`, `"$assoc_array"`, `"$mailbox"`) for these —
  SystemVerilog identifiers can never start with `$` (the LRM reserves
  that lexical class for system tasks/functions), so this can never
  collide with a real user type name extracted by `userTypeName()`. Plain
  `int`/`logic`/other built-in scalar types keep the existing
  empty-`detail` convention (no container, no member scope at all).
- **A static built-in-method table, entirely independent of
  `SymbolDatabase`.** `CompletionProvider`'s dot-completion branch
  (`src/lsp/completion.cpp`, added by §6.10) needs a new pre-check before
  calling `findSymbolsInScope`: if the resolved object's `detail` is one of
  the `$`-tagged container markers, build the candidate list from a new
  hand-maintained static table (e.g. `src/lsp/sv_builtin_methods.h`,
  listing the method-name/kind/detail triples above per container kind)
  instead of querying the DB at all, then fuzzy-score/sort exactly like any
  other candidate set (reuse `buildCompletionItems`).
  `CompletionItemKind::Method` (or `Function`) is the natural
  `completionKindFor`-equivalent mapping for these; `item.detail` can carry
  a short signature string (e.g. `"push_back(item)"`) since there's no real
  `ParseRecord` to source it from.
- **Randomize-family methods on ordinary classes.** Once the dot-completion
  branch resolves an object to a real DB `Class` scope (the existing §6.10
  path, unchanged), *always* union in the randomize-family method list
  above as synthetic candidates before scoring — every class gets them
  regardless of what it explicitly declares, so this is a flat addition at
  the call site, not per-class detection. Note the possible name collision
  this creates: nothing stops a user class from declaring its own method
  literally named `randomize` (the LRM allows overriding it) — if
  `findSymbolsInScope` already returned a real, DB-backed `randomize` for
  that class, prefer the real one and skip adding the synthetic candidate
  for that specific name (a simple name-collision check by candidate name,
  not full override semantics).

**Functional test:** declare a queue-typed (`int q[$];`), an
associative-array-typed (`int aa[string];`), and a `mailbox`-typed
(`mailbox #(int) mbx;`) variable, plus an ordinary class instance with no
explicitly-declared `randomize`; trigger completion at `q.`, `aa.`, `mbx.`,
and `obj.` (Emacs); verify each returns exactly its own built-in method set
(e.g. `push_back`/`pop_front` for the queue, `exists`/`num` for the
associative array, `put`/`get` for the mailbox, `randomize`/
`pre_randomize`/... for the class instance, alongside whatever it
explicitly declares) and nothing from an unrelated container kind.

**Unit tests:** `sv_tree_walker` detail-tagging for a queue-typed, an
associative-array-typed, and a plain (non-container) declarator sharing one
`data_type_or_implicit` (the `int a[$], b;` case above); `mailbox`/
`mailbox #(T)` still resolves via `userTypeName()`'s existing `class_type`
path; `CompletionProvider` end-to-end for each of the four `$`-tagged/class
cases above, including the `randomize`-vs-user-declared-`randomize`
collision case.

**Open questions, deliberately unresolved:**
- Whether to also cover dynamic arrays (`int a[]`, detected via
  `unsized_dimension()`) — the LRM gives them `size()`/`delete()`/`new[]`/
  the same §7.12 locator/ordering/reduction methods as queues, minus
  `push_front`/`push_back`/`pop_front`/`pop_back`/`insert`. Not explicitly
  requested; natural to add alongside queues in the same pass given the
  detection mechanism is identical, but left out of the method-list
  research above — enumerate before implementing if pursued.
- `randomize() with { constraints }` is a distinct grammar construct
  (`randomize_call`, not a plain method-call/argument-list) — this section
  only completes the bare `randomize()` method name; completion *inside* a
  `with` block's constraint braces is a different, harder problem (would
  need to resolve back to the class's own `rand`/`randc` member names) and
  is out of scope here.
- Whether `semaphore` (LRM §15.3, another built-in parameterized-class-like
  type: `new`/`put`/`get`/`try_get`) belongs in the same pass as `mailbox`
  — same detection mechanism (`userTypeName()` returning the literal name),
  not researched here, left for whoever picks this up.

---

### 6.14 Function-Call Return-Type Chained Dot-Completion (`func_ret_class().member`, multi-level)

**Status:** implemented 2026-09-02. Extends §6.10/§6.13 (both shipped
2026-09-02) and subsumes §6.10's own deliberately-deferred "chained access
(`foo.bar.baz`)" open question, generalized to include function/method
calls as a segment kind, exactly as originally sketched below.

`dotCompletionContext` (`src/lsp/symbol_utils.h/.cpp`) was generalized in
place — `DotCompletion{object, prefix}` became `DotCompletion{segments,
prefix}` where `segments` is a `vector<ChainSegment>` (`{name, isCall}`) —
rather than adding a parallel mechanism: a 1-segment chain reproduces
today's exact single-hop behavior, so this is a strict superset, not a
second code path. The paren-balance/string-literal-aware call-segment
walk was verified by hand-tracing both of this section's own test
examples (nested `a(b(c)).d` and quoted `foo("a.b").c`) during planning,
the same "don't trust reading the grammar/parser alone, verify by trace or
probe" discipline §6.13 established for the ANTLR grammar, applied here to
a hand-rolled parser instead.

Segment resolution (`src/lsp/completion.cpp`, `resolveFirstSegment`/
`resolveMemberSegment`/`resolveChain`) matches this section's original
design: segment 0 resolves against what's visible at the cursor (`this`/
`super` special-cased via a new `SymbolDatabase::enclosingClassNameAt`,
a call via `Function`-kind lookup, a bare identifier via `Signal`/
`Parameter` lookup — all reusing `findSymbolsVisibleAt`'s existing
precedence); every later segment resolves as a member of the previous
segment's resolved class via `findSymbolsInScope`. `super` reads the
enclosing class's own parent-class `detail` (via `findSymbolsByName` +
`pickBestSymbol`, the same cross-file disambiguation hover/definition
already use), not the class itself — proven by a dedicated test where the
child class overrides the very method being chained through.

**A simplification found while implementing, replacing this section's own
recommended approach:** the hand-maintained built-in-type-keyword list
below (option 1) turned out to be unnecessary. SV reserved words can never
be valid identifiers, so a `Function`'s raw, unfiltered return-type text
(`"void"`, `"int unsigned"`, ...) can *structurally never* collide with a
real class/scope name anywhere in the DB — every lookup on that raw text
already comes back empty on its own, with no false-positive risk, so
every hop just feeds whatever it resolved to straight into the next
lookup uniformly, whether it came from a `userTypeName()`-filtered
Signal/Parameter `detail` or a Function's raw `getText()` return type. No
keyword list, no separate single-identifier-token pre-check, no schema
change — removing an entire piece of this section's original design (and
a file that would otherwise need hand-maintaining and cross-checking
against the LRM, on top of §6.13's own already-disclosed uncertainty)
for free. This did surface one real landmine the keyword-list approach
would have sidestepped by accident: `""` is not "unknown type" in this
schema, it is the literal *top-level scope* value every top-level symbol
is stored under, so `findSymbolsInScope("")` returns the whole project's
top-level symbols rather than nothing — every hop (not just the first, as
§6.10 alone required) must explicitly guard against an empty resolved
type, now centralized in one place (`candidatesForResolvedType`).

The §6.13 candidate-building logic (`builtinMethodsFor` dispatch, else
`findSymbolsInScope` + the `Class`-gated `RANDOMIZE_METHODS` union) was
extracted into that one shared `candidatesForResolvedType(db, detail)`
function, called once after chain resolution regardless of chain length —
there is no longer a separate "single-hop" branch in `getCompletion`.
This is a behavior-preserving refactor: every pre-existing §6.10/§6.13
test passed unchanged after it.

Tests: `tests/unit/lsp/test_symbol_utils.cpp` (chain-parsing cases,
including the nested-call and string-literal-argument cases traced by
hand above); `tests/unit/db/test_symbol_database.cpp`
(`enclosingClassNameAt`, including nested-inside-a-method); new
`CompletionProvider` cases in `tests/unit/lsp/test_completion.cpp`
(2-level identifier chain, 3-level call chain, a broken link failing
closed, `this`/`super` including the override-vs-parent distinguishing
case, and a chain ending on a §6.13 built-in container member to prove
`candidatesForResolvedType` is genuinely shared); functional coverage in
`tests/integration/test_30_chained_dot_completion.sh` + fixture
`tests/integration/fixtures/chained_dot_completion.sv`.

Original sketch (superseded by the above except where noted, kept for history):

**Status (original):** not started. Extends §6.10 (dot/member-access completion) and,
once landed, subsumes §6.10's own deliberately-deferred "chained access
(`foo.bar.baz`)" open question — this section is where that gets resolved,
generalized to include function/method calls as a segment kind.

**Why this is needed:** §6.10 only resolves a single bare-identifier object
immediately before the last `.` (`foo.bar`); `foo.bar.baz` was explicitly
deferred, and a call result was never in scope at all
(`func_ret_class().member`, or a method call mid-chain,
`obj.get_child().greet`). Real verification code chains through factory
methods, builders, and accessor methods constantly — a completion feature
that only ever works one hop off a plain variable misses a large share of
where `foo.` completion is actually typed in practice.

**LRM vs. real-world simulator support (struct scoping note):** the LRM
allows selecting a member directly off a function call's result for
*structs* as well as classes (a function returning a `struct` type, then
`.field` chained straight off the call). Per direct experience, this isn't
actually implemented by the major commercial simulators — struct-returning
function calls don't support chained member access there even though the
LRM permits it. This section's design should keep that door open (the
segment-resolution mechanism below is generic over "does this type have a
member scope," not class-specific in principle) but the **scope of
implementation here is classes only**, matching real-world simulator
behavior rather than the letter of the LRM; add struct support later only
if that ever changes or a concrete need shows up, not preemptively.

**What is missing — chain parsing:**
- **Recovering a multi-segment chain, not just one identifier.** `foo.bar.baz`,
  `func_ret_class().member`, and `obj.get_child().greet` all need the text
  left of the cursor walked as a sequence of segments split on top-level
  `.`, where each segment is either a bare identifier or
  `identifier '(' ... ')'` — and the `(...)` itself must be walked with
  paren-balance tracking (an argument can itself contain nested calls,
  strings, or literal `.`s, e.g. `foo(bar.baz, "a.b").member`). This is a
  materially harder parse than §6.10's `dotCompletionContext` (a single
  identifier-chars-only left-walk); the closest prior art in this codebase
  is the preprocessor's existing paren/brace/bracket-balance-aware
  macro-argument splitting (`src/compiler/sv_preprocessor.cpp` — reuse the
  *technique*, not the code, since that operates on preprocessor tokens,
  not a raw document-text left-walk from a cursor position). Comments and
  string literals earlier on the same line need to be respected the same
  way `wordAtPosition`/`dotCompletionContext` already ignore them implicitly
  (they only ever look at the literal characters, no lexing) — worth an
  explicit test since a `)` or `.` inside a string argument must not
  confuse the paren-balance walk.
- **Segment-by-segment scope resolution, iterating left to right.** Segment
  0 resolves like today (§6.10's variable lookup) if it's a bare
  identifier, or via a **new** lookup if it's a call: find a `Function`
  visible at this position by name (`findSymbolsVisibleAt`, same
  scope-chain precedence already used for variable resolution) and resolve
  *its* return type instead of a variable's declared type. Each later
  segment resolves as a **member** of the previous segment's resolved class
  scope (`findSymbolsInScope(prevClass)`): a bare identifier segment looks
  up a `Signal`/`Parameter` member and reads *its* declared-type `detail`
  (already populated by §6.10); a called segment (`identifier(...)`) looks
  up a `Function` member and reads *its* return type. The final segment
  (whatever's after the last `.`, being typed right now) is the fuzzy-match
  prefix against the last resolved class scope's members — unchanged from
  §6.10 from that point on. A resolution failure at *any* segment (unknown
  name, non-class return type, wrong kind) aborts the whole chain and
  returns no completions, same "fail closed" posture as §6.10.
- **The `detail`-as-return-type ambiguity this surfaces.** Function's
  `detail` today stores the **raw** return-type text verbatim
  (`enterFunction_body_declaration`, `retType = fdt->getText()`) —
  deliberately unfiltered, unlike Signal/Parameter's §6.10 `userTypeName()`
  convention, because hover displays it as-is and a raw `"void"`/`"int
  unsigned"`/`"logic [7:0]"` is exactly the useful information a user wants
  to see there. Chain resolution instead needs to know "is this return type
  a class I can look members up in," which raw text alone doesn't answer
  cleanly (`"void"` and `"int"` are non-empty but not classes; a genuine
  bare class name looks textually identical to a genuine bare `typedef`
  name). Three ways through, in recommended order:
  1. **(Recommended)** At the resolution call site, re-check the already-
     stored raw `detail` against a small hand-maintained list of SV
     built-in data-type keywords (`void`, `logic`, `int`, `byte`, `bit`,
     `shortint`, `integer`, `longint`, `time`, `real`, `shortreal`,
     `realtime`, `string`, `chandle`, `event`, `signed`, `unsigned`, ...) —
     a bare single-token `detail` not in that list is treated as a
     candidate class name (verified for real by whether
     `findSymbolsInScope` actually returns anything); anything
     multi-token/bracketed (`"int unsigned"`, `"logic [7:0]"`) fails a
     single-identifier-token check trivially and is correctly never
     treated as a class. Small, stable, no schema change, and the list is
     a strict subset of whatever §6.9's full keyword list ends up being
     (share it from there if §6.9 lands first).
  2. Give `SvTreeWalker` a second, `userTypeName()`-style *filtered*
     extraction for function return types, stored in a **new**
     `ParseRecord`/schema field dedicated to "resolvable type scope,"
     decoupled from `detail`'s existing raw-text/display role. Cleaner
     separation of concerns, but a real schema/migration cost
     (`src/db/schema.h`, `SCHEMA_VERSION` bump, `SymbolRow`, every query
     that selects `detail` today) for a problem option 1 solves without one.
  3. Reuse `userTypeName()`'s filtered extraction *in place of* the current
     raw `getText()` for `detail` itself — rejected: would regress hover's
     current, useful display of a function's exact return type (including
     `void`/dimensions/qualifiers) for every function, not just
     class-returning ones. The same mistake §6.10 deliberately avoided for
     `Port`'s `detail`; don't repeat it here for `Function`.
- **`CompletionProvider` integration.** Generalize §6.10's dot-branch from
  "resolve one object, then look up its scope" into "resolve a chain of
  segments, threading the resolved class scope through each hop," reusing
  `findSymbolsInScope` and `buildCompletionItems` unchanged at the end.
- **`this`/`super` as segment 0.** In scope for this section (not deferred):
  `this.method().field` and `super.method().field` inside a class body need
  their own special case, since `this`/`super` aren't ordinary declared
  variables and the normal segment-0 identifier/call lookup won't find
  them. When segment 0's text is literally `this`, resolve directly to the
  enclosing class scope via `scopeAtPosition` (the same call §6.10's own
  `findSymbolsVisibleAt` already makes internally) instead of a
  `findSymbolsVisibleAt`/`Function`-return-type lookup — no DB query needed
  beyond that. `super` resolves the same way but one level up: look up the
  enclosing class's own `Class` `ParseRecord` and read *its* `detail`
  (already populated with the parent-class name — see `ParseRecord::detail`'s
  doc comment, "class parent" — by whatever emits `Class` records today),
  then use *that* as the starting scope instead of the enclosing class
  itself. Both are then just a normal segment-0 resolution result feeding
  into the same iterative member-resolution loop as any other chain — `this`/
  `super` only special-cases *how* segment 0's scope is found, nothing
  downstream of it.

**Functional test:** three-level chain, e.g.
`class Greeter; function void greet(); endfunction endclass`,
`class Factory; function Greeter get_child(); endfunction endclass`, a
free function `function Factory make_factory();`, then trigger completion
at `make_factory().get_child().gr` (Emacs) and verify `greet` is the only
candidate; also verify a broken link (e.g. a function returning `int`
mid-chain) yields no completions rather than falling back to scope-wide
completion. Also cover `this`/`super`: inside a method body, trigger
completion at `this.get_child().gr` and verify it resolves the same as the
free-function chain above; with `Factory` extending a `BaseFactory` that
itself declares `get_child`, trigger completion at `super.get_child().gr`
from inside a `Factory` method and verify it resolves through the parent
class, not `Factory`'s own (potentially overriding) `get_child`.

**Unit tests:** chain-parsing helper on 2-level (`foo.bar.b|`) and 3-level
(`a().b().c|`) inputs, including a call argument containing a literal `.`
or `)` inside a string (`foo("a.b").c|`) to prove the paren-balance walk
isn't confused by it; segment resolution for {bare identifier, call} ×
{first segment, later segment} — four combinations; `this`/`super` as
segment 0 resolving to the enclosing class / its parent class
respectively; the built-in-type-keyword-list check rejecting `void`/`int`-
returning intermediate calls; `CompletionProvider` end-to-end for the
3-level functional-test chain above, the `this`/`super` functional-test
cases, plus a broken-link negative case.

**Open questions, deliberately unresolved:**
- Struct support (see LRM-vs-simulator note above) — deliberately out of
  scope; the segment-resolution design isn't class-specific in principle
  (a struct's fields could in theory be looked up the same way if this
  codebase ever gains struct-member `ParseRecord`s of its own, which it
  doesn't today), so revisit only if that changes.
- Static/class-scoped calls (`Factory::make()`, using `::` rather than a
  preceding `.`) as segment 0 — a different call syntax than an ordinary
  function call or instance method call; not addressed here, left as a
  follow-up if it turns out to be common enough to matter.
- How deep a chain to support in practice — no hard limit is proposed;
  the design above is naturally recursive/iterative per segment, so depth
  isn't expected to need a cap, but this hasn't been stress-tested against
  a pathological input (e.g. a very long chain) for parse-time cost.

---

### 6.15 Queue/Associative-Array Element Access Completion (`list[a].member`, including multi-dimensional)

**Status:** implemented 2026-09-03, following the design below essentially
as sketched (option 1 — layered `detail` string, no schema change).
Extends §6.13 (built-in container method completion) and §6.14 (chained
dot-completion), both shipped 2026-09-02. Multi-dimensional access
(`arr[i][j].member`) was in scope from the start and is covered, including
the partial-indexing case (`arr[i].` on a fixed-array-of-queues correctly
lands on the queue's own methods).

**As implemented:** `containerDimensionTag()` (singular) became
`containerDimensionTags()` (plural, `src/compiler/sv_tree_walker.cpp`),
walking every `variable_dimension()` entry instead of stopping at the
first; `enterData_declaration` joins those tags with the element
type/tag (`userTypeName()`/`builtinBareTypeTag()`, whichever is non-empty)
into one `:`-delimited `ParseRecord::detail` string, outermost layer
first — exactly the `"$fixed_array:$queue:MyClass"` shape the design
sketched. `ChainSegment` (`src/lsp/symbol_utils.h`) gained an `indexDepth`
field (default 0); `dotCompletionContext` (`src/lsp/symbol_utils.cpp`)
detects a segment ending in `]` by bracket-balance-matching left with a
new `matchingOpenDelim` helper (a generalization of §6.14's
`matchingOpenParen` to an arbitrary open/close delimiter pair, now shared
by both), repeating for each further consecutive `]` to accumulate the
depth — `arr[i][j]` is one segment with `indexDepth=2`, not two segments,
matching the design. Resolution (`src/lsp/completion.cpp`) adds
`peelDimensionLayers(detail, depth)`: splits `detail` on `:`, counts the
*leading run* of recognized container-dimension tags (`isContainerDimensionTag`),
fails closed (`""`) if `depth` exceeds that count (over-indexing, or
indexing a non-container detail at all), otherwise returns the remaining
layers rejoined. `resolveFirstSegment`/`resolveMemberSegment` both pipe
their resolved row's `detail` through this before returning it —
`indexDepth=0` (every pre-§6.15 segment) is a no-op, so this is a strict
superset of §6.14's resolution exactly as intended.

**One addition beyond the original sketch, found while implementing:**
`candidatesForResolvedType`'s builtin-method dispatch (`builtinMethodsFor`)
now keys off a new `firstLayer(detail)` helper (the substring before the
first `:`) rather than the full `detail` string. This was required to keep
*unindexed* container access working at all once `detail` could carry
extra element-type layers — `"q."` on `MyClass q[$]` now has
`detail="$queue:MyClass"` (previously just `"$queue"`), so exact-string
dispatch would have silently broken every existing §6.13 test without
this. A useful side effect, not separately requested: this also makes
partial-indexing "land on a container, not yet the element" (e.g.
`arr[i].` on a fixed-array-of-queues) correctly offer that container's own
methods via the same mechanism, and (untested but falls out for free from
the same general mechanism) a queue of `string`/`event`/`mailbox`-family
elements now also offers *those* types' own methods one level of
indexing in, not just class elements.

**Confirmed via both layers of testing**, per this project's working
rule: `tests/unit/compiler/test_sv_listener.cpp` (layered-detail
construction, multi-dim and single-dim, class/string/int elements),
`tests/unit/lsp/test_symbol_utils.cpp` (indexed-segment parsing incl. a
`.` inside a string-literal index key, mixed indexed/non-indexed chains,
unmatched-bracket/no-identifier-before-bracket fail-closed cases), and
`tests/unit/lsp/test_completion.cpp` (full resolution incl. partial vs.
full multi-dimensional indexing, over-indexing, indexed access deep in a
longer chain) at the unit level; `tests/integration/test_31_queue_element_completion.sh`
+ `fixtures/queue_element_completion.sv` at the real JSON-RPC/Emacs level.
Unit suite: 1414 assertions / 500 cases, all green (debug). Emacs
integration suite: 186/186, no regressions.

**Why this is needed:** §6.13 gave queues/associative/dynamic/fixed-size
arrays their own container-level method completion (`list.` →
`push_back`/`exists`/...), but indexing into one to reach a specific
*element* and completing on that element doesn't work — `MyClass q[$];`
declares a queue of class-typed objects, but `q[0].` offers nothing today,
because `containerDimensionTag()` (`src/compiler/sv_tree_walker.cpp`)
looks at a declarator's dimensions only far enough to find the *first*
one and returns a single tag — it discards both the element type `T` and
any further nested dimensions (`int arr[4][$]` — an array of queues —
records only `$fixed_array`, silently losing the `[$]` entirely).

**What is missing:**
- **Tracking the *full, ordered* dimension list plus the base type, not
  just one tag.** `ParseRecord::detail` currently holds exactly one
  payload per kind (§6.10's `userTypeName()` class name, §6.13's `$`-tag,
  or a Function's raw return type) — a multi-dimensional declarator like
  `MyClass arr[4][$]` needs *all three* facts at once: outermost shape is
  a fixed array, of queues, of `MyClass`. Two ways through, same tradeoff
  §6.14 already worked through for its own not-taken schema-change option:
  1. **(Likely simplest)** Encode the whole layered shape in `detail` as
     an ordered, delimited list, outermost first, ending in the base
     type/tag — e.g. `"$fixed_array:$queue:MyClass"` for the example
     above, or just `"$queue:MyClass"` for a plain `MyClass q[$]`. No
     schema change; `":"` is a safe delimiter since every layer is either
     a fixed `$`-tag or a bare identifier (`userTypeName()` already never
     produces a qualified/`::`-containing name — the same
     already-accepted simplification, not a new one).
  2. A dedicated new `ParseRecord`/schema field for the layered shape,
     decoupled from `detail`'s tag role — cleaner separation, real
     schema/migration cost (`SCHEMA_VERSION` bump, `SymbolRow`, every
     query selecting `detail`).
  `containerDimensionTag()` becomes `containerDimensionTags()` (plural):
  walk *every* entry in `vda->variable_dimension()`, not just the first,
  building the ordered list.
- **A third chain-segment kind, with a depth.** `dotCompletionContext`'s
  `ChainSegment{name, isCall}` (`src/lsp/symbol_utils.h`) only
  distinguishes a bare identifier from a call (`identifier(...)`);
  indexed access needs a third shape carrying *how many* consecutive
  bracket groups followed the identifier (`arr[i][j]` → depth 2, not two
  separate segments — there's no `.` between the brackets). Parsing walks
  right to left same as today: after finding `]`, bracket-balance-match
  back to `[` (extending §6.14's `matchingOpenParen` technique to `[`/`]`
  instead of `(`/`)`), then check whether the character immediately before
  *that* `[` is another `]` — if so, repeat and increment the depth,
  until hitting the identifier itself. Each index expression (`i`, `j`,
  ...) is never resolved or type-checked, only its presence and count
  matter — same "arguments are opaque" posture §6.14 already established
  for call segments.
- **Resolution: peel N layers, don't require a full resolve.** When a
  segment carries an index depth, resolve the identifier/member exactly
  as before to get its full layered `detail` string, then strip exactly
  `depth` layers off the *front* of it (splitting on `:`). This naturally,
  elegantly handles under-indexing a multi-dimensional container without
  any special-casing: `arr[i].` on `MyClass arr[4][$]` (fixed array of
  queues) peels only the outer `$fixed_array` layer, landing on `$queue`
  — correctly offering the *queue's own methods* (`push_back`, ...),
  since `arr[i]` really is a queue, not yet a `MyClass`. `arr[i][j].`
  peels both layers, landing on `MyClass`, offering its real members.
  Fails closed exactly as every other §6.14 hop does if `depth` exceeds
  the number of layers available, or the layer landed on is a built-in
  type with nothing to offer (e.g. `int q[$]; q[0].` still correctly
  yields nothing). This slots into §6.14's existing
  `resolveFirstSegment`/`resolveMemberSegment` iteration as a third case,
  not a new mechanism — `candidatesForResolvedType` already handles both
  a landed-on container tag and a landed-on class uniformly, so no change
  needed there.
- **Covers fixed-size and dynamic arrays too**, for the same reason §6.13
  covered them alongside queues/associative arrays — same underlying
  per-declarator dimension detection, same element-type-tracking gap.

**Explicitly still out of scope:** associative-array key-type validation
(any index expression is accepted, never checked against the array's
declared index type); indexing directly off a *call's* result
(`get_matrix()[i][j].member`) — a natural further generalization (the
depth-carrying segment kind would need to attach to a call segment too,
not just a bare identifier) but a distinct extension from "multi-
dimensional array/queue," not requested here, left for a follow-up;
struct-typed elements (same **classes only** restriction §6.14 already
established, for the same real-world-simulator-behavior reason).

**Functional/unit tests (sketch):** a queue and an associative array of a
class type, each with a known member, exercising single-dimension
`q[0].`/`aa[k].`; a genuinely multi-dimensional declarator (`MyClass
arr[4][$]` or similar) exercising both *partial* indexing (`arr[i].` →
queue methods) and *full* indexing (`arr[i][j].` → the class's members) —
this partial-vs-full distinction is the crux of the design and needs
direct coverage, not just the fully-indexed case; a built-in-typed
element (`int q[$]; q[0].`) still yields no completions (regression
guard, same fail-closed posture); a plain container method access
(`q.`, no index at all) is unaffected by this section's own changes.

---

### 6.16 Function/Task Prototype Recording (pure virtual, extern, interface-class methods, DPI import)

**Status:** implemented 2026-09-03, following a real user bug report.

**Why this was needed:** a user reported that `all_queue[i].get_policy`
offered no completions, where `all_queue` was a queue of `policy_base` and
`get_policy` was declared `pure virtual function policy_base
get_policy(uvm_object par);`. Investigation (direct JSON-RPC probes
against the real binary, both on the user's real files and on minimal
from-scratch repros — this project's own established "verify empirically,
don't just read the grammar" discipline, see §6.13) found that
`src/compiler/sv_tree_walker.cpp` never recorded a symbol for *any*
function/task declaration without a body. Only
`enterFunction_body_declaration`/`enterTask_body_declaration` existed,
both requiring `function_data_type_or_implicit ... 'endfunction'`/`...
'endtask'` — the grammar's separate `function_prototype`/`task_prototype`
rules (no body, terminated by `;`) had no listener at all. This silently
broke hover/definition/completion/references for any `pure virtual`
method, `extern`-declared method, or interface-class method (interface
classes are *always* prototype-only) — a common pattern in UVM-style
verification code (17 files in a real UVM checkout use `pure virtual
function`/`task`).

A second, related bug was found while narrowing the repro:
`interface_class_declaration` (`grammar/Sv.g4`, `// ROOT node` comment)
was defined but never referenced from any reachable parent rule —
completely dead grammar. Confirmed against the user's own real project
file (`interface class policy_api; ... endclass;` inside a package):
produced spurious "extraneous input 'interface'..." parse errors. ANTLR's
error recovery discarded the unexpected `interface` token and happened to
reparse the remainder as an ordinary `class_declaration`, which is why it
silently "mostly worked" (misclassified, alongside false-positive
diagnostics) rather than failing loudly.

**What was implemented:**
- `grammar/Sv.g4`: added `| interface_class_declaration` to
  `package_or_generate_item_declaration` — the reported failure shape (an
  interface class declared inside a `package`, matching LRM Annex A.2.1.3
  and the user's own file). Deliberately not wired into
  `module_or_generate_item`/`anonymous_program_item`/top-level
  `description` — not the reported failure shape, and expanding reach
  without a concrete failing case risks unintended ambiguity for no
  demonstrated benefit.
- `src/compiler/sv_tree_walker.cpp`: one new listener pair,
  `enterFunction_prototype`/`exitFunction_prototype` and
  `enterTask_prototype`/`exitTask_prototype`, modeled directly on the
  existing body-form listeners. **Covers pure-virtual, `extern`,
  interface-class-method, and DPI-import shapes uniformly, with no
  per-context dispatch** — ANTLR generates one listener callback per
  *grammar rule*, firing regardless of which parent alternative reached
  it, and all four shapes reduce to the same `function_prototype`/
  `task_prototype` rules (confirmed via the generated `SvParser.h`'s exact
  accessor shapes — `function_identifier()`/`data_type_or_void()`/
  `tf_port_list()` — before writing any listener code, not assumed).
  Mirrors §6.14's own found simplification: reuse what the grammar's
  structure already gives you rather than dispatching per parent context.
  A DPI-imported function picking up a symbol too is a harmless,
  arguably-correct bonus, not scope creep.
- `src/compiler/sv_tree_walker.cpp`: `enterInterface_class_declaration`/
  `exitInterface_class_declaration`, modeled directly on
  `enterClass_declaration`/`exitClass_declaration`, reusing
  `ParseRecordKind::Class` — no new enum value, no schema change, no
  dispatch changes anywhere else (`symbol_utils.cpp`'s
  `symbolKindFor`/`completionKindFor`, `pickBestSymbol`'s
  `isDeclarationLikeKind`, and every dot-completion resolution path
  already treat `Class` uniformly). An interface class can `extends`
  multiple other interface classes; only the *first* listed parent is
  recorded in `detail` (and thus reachable via `super.`), matching the
  single-inheritance assumption `super.` resolution already makes for
  ordinary classes project-wide — a disclosed, tested limitation, not an
  oversight.

**The one non-obvious pitfall, found by reading `pushId()` closely (not by
inspection alone):** `pushId` unconditionally pushes a new scope frame for
`ParseRecordKind::Function`/`Task`, on the premise a body will eventually
pop it via the existing body-form exit listeners. A prototype has no body
— without a matching `exitFunction_prototype`/`exitTask_prototype` popping
that frame immediately, every scope pushed for a pure-virtual/extern/
interface-class method would leak forever, silently corrupting scope
attribution for every symbol declared afterward in the file (structurally
the *same* corruption symptom the user's own unrelated `` `import ``-typo
bug independently demonstrated is possible here). No `backpatchEndLine`
call is needed in the new exit listeners (unlike the body-form exits): a
leaf/no-body record correctly keeps `endLine == 0`, already
`ParseRecord::endLine`'s convention for Port/Signal/Parameter/Macro. A
dedicated regression test (a pure-virtual method followed by an ordinary
declaration, asserting the second one's `scope`/`parent` didn't leak)
guards this specifically — it would have caught the leak before shipping.

**No LSP-layer code changed at all:** because both new record kinds reuse
existing `ParseRecordKind::Function`/`Task`/`Class`, every consumer
(`hover.cpp`, `definition.cpp`, `document_symbols.cpp`, `completion.cpp`
including every §6.10–§6.15 dot-completion path) needed zero changes —
purely additive in the compiler front-end.

**Verification:** unit tests (`tests/unit/compiler/test_sv_listener.cpp`)
cover pure-virtual function/task recording, `extern` prototypes, the
scope-leak regression, a full interface-class declaration (zero
diagnostics), multiple interface inheritance's first-parent-only
`detail`, a class extending an unresolved external base (`uvm_object`,
modeling the real bug shape), and DPI import; one confirmatory test in
`tests/unit/lsp/test_completion.cpp` proves the completion pipeline needs
no changes for an `endLine == 0` `Function` row. Functional test
`tests/integration/test_33_prototype_methods.sh` +
`fixtures/prototype_methods.sv` cover the same shapes end-to-end (zero
diagnostics, indexed dot-completion onto a pure-virtual method, hover on a
prototype's own declaration). Also re-verified directly against the
user's real, motivating project files: both previously-invisible methods
(`get_policy`, `check_parent_type` on `policy_base`; all three methods on
the real `policy_api` interface class) now show up correctly, with zero
diagnostics.

**Explicitly still out of scope:** `class_constructor_prototype` (`extern
function new(...)`, a distinct grammar rule from `function_prototype`) —
constructors aren't recorded as symbols at all today, a separate,
pre-existing gap unrelated to this fix, not attempted here. A **real,
separate bug found in the same investigation, deliberately not fixed
here** — dot-completion can never resolve into a class declared inside a
package at all (`findSymbolsInScope`'s exact-match `scope` lookup can
never match `userTypeName()`'s deliberately-unqualified `detail`) — see
§6.17 below.

### 6.17 Dot-Completion Into a Package-Nested Class's Members

**Status:** implemented 2026-09-03, same day it was found while building
§6.16's functional test. Chose option (b) from this section's own
original sketch (a qualified-lookup fallback at the completion-resolution
call sites) over (a) (making `detail` itself carry a qualified name
everywhere) — narrower, and doesn't touch what every other `detail`
consumer (hover formatting included) already assumes it means.

**Why this was needed:** `findSymbolsInScope(scope)`
(`src/db/symbol_database.cpp`) does an exact `s.scope = ?` match against
the fully-qualified scope chain (e.g. `"policy_pkg::PolicyImpl"` for a
class declared inside a package). But a class-typed `Signal`/`Parameter`'s
own `detail` — what dot-completion resolution feeds into
`findSymbolsInScope` — comes from `userTypeName()`
(`src/compiler/sv_tree_walker.cpp`), which by design "already never
produces a qualified/`::`-containing name" (a simplification going back to
§6.10, previously harmless). For any class declared inside a `package`,
that bare, unqualified `detail` could therefore never match the member
rows' fully-qualified `scope`, so resolution always fell through to
"not a real class" — a class's real members were silently invisible to
`.` completion, offering only the synthetic `randomize`-family methods at
best. Confirmed directly on a real project file before fixing: a class
extending `uvm_object`, declared inside a package, with its own
pure-virtual method, offered only `get_randstate` via dot-completion,
never the real method. Every dot-completion fixture in this repo happened
to declare its classes at top level (bare name and scope chain trivially
coincide there), which is why this went uncaught despite essentially
every real UVM/verification class living inside a package.

**As implemented:** one new helper, `qualifiedClassScope(db, className,
curPath)` (`src/lsp/completion.cpp`), resolves a bare class name to the
fully-qualified scope chain its members are actually stored under: finds
the class's own DB row by name (`findSymbolsByName`, filtered to
`kind == "Class"`), disambiguates with `pickBestSymbol` — the exact same
same-file-then-first-row logic hover/definition/`super.` resolution
already use — then returns `row.scope.empty() ? row.name : row.scope +
"::" + row.name`. For a top-level class this reproduces the bare name
unchanged (`scope` is `""`), so every pre-existing test kept passing with
zero fixture changes. Called at both places that previously fed an
unqualified `detail`/`prevClass` straight into `findSymbolsInScope`:
`candidatesForResolvedType` (the terminal hop of a chain) and
`resolveMemberSegment` (every *intermediate* hop) — both needed the fix
independently, since a package-nested class can be reached either way
(`obj.child.greet` hits `resolveMemberSegment` for `child` before ever
reaching the terminal `greet` lookup). `resolveChain`/`getCompletion` were
updated only to thread the current file path (`curPath`) through to both
call sites — no behavioral change of their own. `resolveFirstSegment`'s
`this`/`super` branches needed no change at all: they already return a
bare class name (`enclosingClassNameAt`'s own query is `SELECT s.name`,
never qualified), so whatever they hand back flows into one of the two
now-fixed call sites like any other segment, fixing `this.`/`super.` on a
package-nested class for free.

**A real simplification fell out of the fix, not just the fix itself:**
`candidatesForResolvedType` used to run a *second*, independent
`findSymbolsByName(detail)` scan purely to gate whether `detail` named a
real `Class` (for the randomize-family union). `qualifiedClassScope`
returning non-empty already proves exactly that, so that separate scan
was deleted — the randomize union now runs unconditionally once past the
`qualified.empty()` guard.

Container/type tags (`$queue`, `$queue:MyClass`, mailbox/semaphore/
process/string/event literal names, ...) and a Function's raw built-in
return-type text (`"void"`, ...) are unaffected at both fixed call sites:
`qualifiedClassScope` calls `findSymbolsByName` on the literal text, which
can never match a real symbol name for any of these (SV identifiers can't
start with `$` or contain `:`; reserved words can never be identifiers —
the same non-collision reasoning §6.14 already established), so it
correctly returns `""` and preserves the exact same fail-closed behavior
they already had — confirmed by every existing §6.13/§6.15 test
continuing to pass unmodified, not just by reasoning about it.

**Verification:** 5 new unit tests
(`tests/unit/lsp/test_completion.cpp`) — the core single-hop regression,
an intermediate-hop regression (`obj.child.greet` with `child`'s class
package-nested, proving `resolveMemberSegment`'s own fix independently of
`candidatesForResolvedType`'s), `this.` through a package-nested enclosing
class, a top-level-class backward-compatibility guard, and an
intermediate hop resolving to a genuinely bogus type still failing closed
(exercising `resolveMemberSegment`'s new guard specifically, which the
pre-existing single-hop-only fail-closed test never touched). Functional
test `tests/integration/test_34_package_scoped_dot_completion.sh` +
`fixtures/package_scoped_dot_completion.sv` re-creates the real
motivating shape end-to-end. Also re-verified directly against the user's
real, original bug report (`all_queue[i].get_policy`,
`/home/martin/src/policy/policy_mixin.sv`): now completes to `get_policy`
correctly — combined with §6.16, this fixes the user's *exact* original
repro completely, even before they've fixed the unrelated `` `import ``
typo still present in that file (its parse-error cascade turned out not
to disrupt `all_queue`'s own scope tracking after all).

---

### 6.18 Recompile on Save (`textDocument/didSave`)

**Status:** implemented 2026-09-12, following this section's own sketch exactly.

**Why this is needed:** `registerHandlers()` (`src/lsp/server.cpp:62-...`) wires
`didOpen`/`didChange`/`didClose` but has no `TextDocument_DidSave` handler at
all -- the server advertises `save = true` in its `textDocumentSync`
capability (`src/lsp/server_state.cpp`, part of the `handleInitialize`
response) but silently drops every `didSave` notification a client sends as a
result, since no handler is registered for it. Today, freshness relies
entirely on §6.8's debounced `didChange` compile (a 300ms quiet period after
the last edit). In the common case this already leaves the file compiled
*before* the user saves, so this is not fixing a correctness bug — but two
real gaps exist: (1) a save that follows closely on the heels of an edit (e.g.
paste-then-immediately-save, or an editor/keybinding that saves on every
buffer-focus-loss) can land inside the still-pending 300ms window, so the
diagnostics visible immediately after save briefly reflect the *pre*-edit
state; (2) `didSave` is the natural, infrequent trigger point for eventually
recompiling *dependent* files once §6.4 (Cross-File Invalidation) lands —
propagating on every debounced keystroke would be wasteful and fights §6.8's
own "wait for typing to pause" intent, whereas "recompile dependents when the
file is actually saved" matches how every mainstream language server (clangd,
rust-analyzer) scopes that propagation.

**Implemented as:**
- A new `lsp::notifications::TextDocument_DidSave` handler in
  `registerHandlers()` (`src/lsp/server.cpp`), placed just ahead of the
  existing `didClose` handler: cancels any pending debounce entry for that
  URI (`m_debouncer.cancel(uri.toString())`, the same call `didClose` already
  makes) so a stale, already-superseded timer can't fire a redundant publish
  right after, then calls `compileAndPublish(uri)` synchronously/immediately
  — giving an unconditional fresh compile at the moment of save regardless of
  where in the debounce window it lands. Exactly as sketched: no
  `CompilationController`/DB changes needed, since this only changes *when*
  `compileAndPublish` runs, not what it does. `DidSaveTextDocumentParams`'s
  optional `text` field is unused — the server never requested
  `includeText` (`server_state.cpp`'s `save = true` is the plain-boolean
  form), and `compileAndPublish` already reads the current text from
  `m_store` (kept fresh by the preceding `didChange`), matching every other
  call site.

**Unit tests (implemented):** extended `tests/unit/lsp/test_server_debounce.cpp`
with a new case and a `TestClient::didSave`/`hasPendingMessage` helper pair:
schedules a `didChange` (debounced, not yet fired) introducing an error,
sends `didSave` immediately, asserts the diagnostics publish reflects the
latest (post-edit) text and arrives in well under the ~300ms debounce delay
(`elapsed < 250ms`), then polls (via `poll()` on the pipe fd, non-blocking)
past the *original* debounce deadline and asserts no second, stale publish
follows — proving `didSave` actually cancelled the timer, not just raced it.

**Functional test (implemented):** `tests/integration/test_36_recompile_on_save.sh`
— two cases against a scratch temp file (not a tracked fixture, since this
test actually calls `save-buffer`; the temp file also sits outside
`SVLSP_ROOT`, so it gets its own isolated single-file workspace rather than
sharing the rest of the suite's `SVLSP_ROOT`-rooted one): (1) insert a bad
token and save-buffer immediately, poll diagnostics for up to 250ms
(strictly under the 300ms debounce) and confirm they appear; (2) the
reverse — fix the error and save again, confirming diagnostics clear
promptly too.

**A client-side timing subtlety found writing this test:** `lsp-idle-delay`
(the suite's init file leaves it at lsp-mode's own default, 0.5s) governs
how long lsp-mode itself waits for typing to pause before it even *sends*
a buffered `didChange` — longer than the server's own 300ms debounce, so
"insert then save immediately" against the default never got `didChange`
onto the wire before the 250ms poll window closed, regardless of whether
the server-side fix worked. Lowering it (`(setq lsp-idle-delay 0.01)`)
*before* `svlsp-test/open-file` establishes the buffer's connection is
required — lsp-mode captures the delay when it creates its idle timer at
connect time, so setting it afterward on an already-connected buffer is a
no-op. Confirmed real end-to-end latency for the fixed didSave path is
~200-220ms once this is corrected. Restored unconditionally after each case
since it's a global defcustom, not buffer-local.

**Open question, deliberately unresolved:** whether `didSave` should also
force a *bypass* of the file's own hash-cache check in
`CompilationController::compile` (today, saving with no net text change since
the last compile is already a hash-cache no-op, which is correct and should
stay that way) — no known reason to change this, noted only so a future
implementer doesn't "fix" it into an unconditional recompile by accident.
Recompiling *dependent* files on save (gap #2 above) is intentionally left to
§6.4's own design once that section is planned in file-level detail; this
section only adds the `didSave` hook and the file's own immediate recompile.

---

### 6.19 Pre-Built Library Database (attach-and-query, no physical merge)

**Status:** design drafted 2026-09-04, per explicit user direction. Fleshes
out §6.5's own "pre-built/shared DBs for rarely-changing library code" open
question into a concrete design, choosing the **attach-and-query** shape
(§6.5's own "Recommended default," modeled on clangd's per-shard
`MergedIndex`) over a true physical merge with moniker-style keys
(LSIF/SCIP) — the true-merge shape is explicitly out of scope for this pass.
**All three pieces implemented 2026-09-04**: the standalone `--build-db`
CLI mode; referencing an already-built library DB directly (`libraryDbs`);
and lazy build-and-cache from a library's own source config
(`libraryDbSources`). This section's own true-merge alternative remains
explicitly out of scope throughout.

**Why this is needed:** unchanged from §6.5's own rationale — a large
fraction of a real verification project's files (per `handoff.md`'s
UVM-corpus work, ~140 files) is third-party/internal library code (UVM,
VIP) that changes rarely but gets fully recompiled from scratch by every
project that includes it, and again on every server restart, since the live
project DB is `:memory:` (§6.5's DB-persistence open question, directly
above this one, is about that same file staying warm across restarts for
*one* project — this section is about *sharing* one already-compiled result
across many projects/restarts).

**Three new pieces, in dependency order:**

1. **A standalone "build a library DB" mode on the `svlsp` binary — implemented
   2026-09-04, essentially as designed.** `svlsp --build-db <config-path>
   --output <db-path>` produces a library DB ahead of time, independent of
   any editor session. `<config-path>` is either a `.svlsp.json` manifest or
   a `.f`/`.svlsp.f` filelist, dispatched by extension
   (`configPath.ends_with(".json")` — the same rule `ProjectRegistry`
   already uses, just inlined via C++20's `std::string::ends_with` rather
   than duplicating `ProjectRegistry`'s own private `endsWith` helper).

   The actual compile logic was factored into a new
   `LibraryDbBuilder::build(configPath, outputPath, progressLog)`
   (`src/lsp/library_db_builder.h/.cpp`) rather than living in `main.cpp`
   directly — `main.cpp` isn't part of any linkable library, so keeping the
   logic there would leave it unit-untestable, violating this project's own
   "every function has a unit test" working rule. `LibraryDbBuilder` opens a
   `Database` against `outputPath` (a real file — the constructor already
   supported this, `src/db/database.cpp:11`; nothing new at the `Database`
   layer), calls the existing `ProjectCompiler::loadProject(config,
   controller, sdb)`, and returns a `Result{ok, fileCount,
   diagnosticCount, error}` — `diagnosticCount` via one ad hoc `SELECT
   COUNT(*) FROM diagnostics` through `Database::prepare` (already public;
   no new `SymbolDatabase` query method needed for a single CLI-only
   summary count). `main.cpp`'s `buildDb()` is now a thin wrapper: call
   `LibraryDbBuilder::build`, print the one-line summary or error to
   stderr, and exit — never entering the `initialize`/stdio message loop.
   Mutually exclusive with normal server mode on the same invocation
   (`--build-db` requires `--output`; `--output` alone is silently ignored
   and the server starts normally). No `LanguageServer`/`ServerState`
   changes needed. Verified directly against the real
   `multifile_project` integration fixture (`-y`-resolved library file
   included in the compiled count) and a throwaway `.svlsp.json` manifest,
   in addition to the unit tests below.

   Note this deliberately doesn't yet build a *whole* CI-friendly wrapper
   (progress bars, `--force`, `--check-stale`, etc.) — those belong to the
   open question at the end of this section, once pieces 2/3 below give
   them something to act on.

2. **Referencing an already-built library DB directly — implemented
   2026-09-04, essentially as designed** — the "the DB already exists, just
   use it" case, in both project-config formats:
   - `ProjectConfig` (`src/compiler/project_config.h`) gained
     `std::vector<std::string> libraryDbs;` alongside the existing
     `libraryDirs`/`libraryFiles`.
   - `.svlsp.json`: a new `"libraryDbs": ["/path/to/uvm-1.2.db", ...]` array
     key (`ProjectManifestParser`), resolved against the manifest's own
     directory the same way `includeDirs`/`libraryDirs` already are.
   - `.f`: a new switch, `-svlsp_library_db <path>` (chainable, one path per
     occurrence, mirroring `-v`) — **deliberately not spelled `+libdb+`**
     despite matching `+libext+`'s naming convention, since `.f` is
     nominally a real VCS/Questa/Xcelium-portable format and an unprefixed
     `+switch+` risks silently colliding with a real vendor switch of the
     same spelling introduced later. `FilelistParser` already hard-errors on
     any switch it doesn't recognize, so this was purely additive.
   - `SymbolDatabase::attachLibraryDbs(paths)` (`src/db/symbol_database.h/.cpp`)
     does the actual `ATTACH DATABASE ? AS lib<N>` (bound parameter for the
     path — the alias itself can't be parameterized, but it's
     `svlsp`-generated from an array index, never user input, so that's not
     a concern) for each not-already-attached path (a path already attached
     — e.g. a second discovered project sharing the same library — is
     silently skipped, not re-attached under a second alias, which would
     otherwise double-count its rows in every query below). Called from
     `ProjectCompiler::loadProject` (once per project load, before compiling
     any files), so both the live server (via `ProjectRegistry`) and
     `--build-db` (piece 1, which can itself depend on other prebuilt DBs
     while building a new one) get this for free.
   - Four `SymbolDatabase` queries (§5.3) were extended to `UNION ALL`
     across every attached schema — this was exactly the integration work
     §6.5's own prior-art note flagged as "the still-open, no-prior-art
     part": `findSymbolsByName`, `findSymbolsByNamePrefix`, and
     `findSymbolsInScope` now share one new private helper,
     `queryAcrossAttachedDbs(cond, bindValue)`, that runs the same
     single-`?`-placeholder `WHERE` clause against the main schema plus
     `lib0`, `lib1`, ... (schema-qualified `FROM lib0.symbols s JOIN
     lib0.files f ...`), with each caller re-sorting the combined result in
     C++ afterward to reproduce its original `ORDER BY` exactly —
     `findSymbolsVisibleAt`'s existing "SQLite doesn't allow expressions in
     `ORDER BY` after `UNION ALL`; sort in C++" precedent generalized to
     every query here, not just that one. `findSymbolsVisibleAt` itself
     needed its own change (not the shared helper, since its shape has
     multiple *different* `WHERE` clauses per part): its Part 2 (cross-file
     top-level + wildcard-imported-package scopes) and Part 3 (specific
     imports) arms are now generated once per schema (main first, then each
     attached library), while Part 1 (the local scope chain at the cursor's
     own position) stays main-schema-only — a cursor is always inside a
     project's own edited file, never a read-only attached library file, so
     extending Part 1 would add SQL with no possible matches.
     `SQLITE_MAX_ATTACHED`'s default 10-per-connection limit (already noted
     in §6.5) bounds how many distinct `libraryDbs` paths can be attached
     across the whole server session (not just one project — see the
     disclosed limitation below on this being session-wide).

   **Disclosed limitations, not oversights (documented on
   `attachLibraryDbs` itself):** `scopeAtPosition`/`scopeKindAtPosition`/
   `enclosingClassNameAt` (all keyed on a specific `(path, line)`) were
   deliberately left main-schema-only, for the same "cursor is never inside
   a read-only library file" reason as `findSymbolsVisibleAt`'s Part 1.
   `unresolvedInstantiatedTypeNames`/`instantiationsOfType` (drives
   `LibraryResolver`'s `-y`/`-v` module/interface instantiation resolution)
   were **not** extended to search attached DBs — library content reused
   this way is expected to be `import`'d (packages/classes), not
   *instantiated* (design-unit modules/interfaces), matching the plan's own
   motivating UVM/verification-IP use case; a module defined only in an
   attached DB would still be reported unresolved today. `export pkg::*`
   re-export-chain traversal (`collectExportedImports`/`fileIdForPackage`,
   used inside `findSymbolsVisibleAt`) also only ever looks in the main
   schema, so a package declared *inside* an attached DB re-exporting
   another package's contents isn't followed (the attached package's own
   direct top-level/wildcard-imported contents are still fully visible —
   only the *re-export chain starting from inside the attached DB itself*
   isn't). `symbolsForFile`/`diagnosticsForFile` (keyed by exact file path,
   backing `documentSymbol`/diagnostics) are also main-schema-only, so
   opening a library file directly (e.g. by following a definition link
   into one) won't show its own outline/diagnostics — a materially
   different, larger piece of work than the four completion/hover-facing
   queries this pass targeted, deferred.

   **A pre-existing architectural characteristic this inherits, not a new
   one this introduces:** `SymbolDatabase`/`Database` is one connection
   shared across the server's *entire* session (already true before this
   section — e.g. `findSymbolsByName` already searches every compiled
   file's symbols regardless of which discovered project loaded it, and the
   Emacs test harness's own documented caveat about one shared, growing DB
   across every test file already describes exactly this). Attached library
   DBs are visible the same way: once any project in the session attaches
   one, its symbols are visible to every file/project in that same session,
   not scoped to the specific project that requested it. Consistent with
   the existing architecture, not a regression.

   **Verification:** `tests/unit/compiler/test_filelist_parser.cpp` and
   `tests/unit/lsp/test_project_manifest_parser.cpp` cover the new
   config-parsing surface (both formats, including relative-path resolution
   and a missing-argument error for the `.f` switch);
   `tests/unit/db/test_symbol_database_library_attach.cpp` covers the
   attach-and-query behavior directly — each of the four extended queries
   seeing a symbol that exists *only* in a real, file-backed attached DB
   (built via a separate `Database`/`SymbolDatabase` instance, proving this
   isn't just an in-process cache hit), the wildcard-import case reaching
   into an attached package, the dedup guard (attaching the same path twice
   doesn't double-count), two distinct attached DBs both being visible at
   once, and an end-to-end `ProjectCompiler::loadProject` test proving the
   config field actually reaches the attach call. Full unit
   (1520 assertions/538 cases) and Emacs integration (197/197) suites both
   green with no regressions — no new integration test was added
   specifically for this (matching piece 1's own reasoning: nothing here
   changes the LSP-protocol surface a client observes beyond symbols simply
   being present, which the existing hover/definition/completion functional
   tests already exercise structurally).

3. **Lazy build-and-cache, given a library's own *source* config instead of
   a prebuilt DB — implemented 2026-09-04, essentially as designed** — the
   "build me one on demand and remember it" case, distinct from (2):
   - `ProjectConfig` gained a second, parallel list:
     `std::vector<LibraryDbSource> libraryDbSources;` (`src/compiler/project_config.h`)
     where `struct LibraryDbSource { std::string configPath; std::string cachePath; };`
     — `configPath` is another `.svlsp.json`/`.f` describing the library's
     own files (exactly what `--build-db`/`LibraryDbBuilder::build` in (1)
     consumes); `cachePath` is where the resulting DB should be looked for
     / written.
   - `.svlsp.json`: `"libraryDbSources": [{"config": "/vip/uvm/uvm.f",
     "cache": "/var/cache/svlsp/uvm-1.2.db"}]` — a new `readLibraryDbSources`
     helper in `ProjectManifestParser` (an array of `{config, cache}`
     objects, unlike every other field here which is a plain string array),
     resolving both paths against the manifest's own directory.
   - `.f`: `-svlsp_library_db_source <config-path> <cache-path>` — two
     arguments (`FilelistParser`'s existing single-arg `needArg` lambda
     called twice in a row), both resolved against `baseDir`, same
     `svlsp_`-prefixed reasoning as (2).
   - `LibraryDbBuilder` (`src/lsp/library_db_builder.h/.cpp`, already home
     to piece 1's `build()`) gained `resolveLibraryDbSources(config,
     progressLog)`: for each `libraryDbSources` entry, **if `cachePath`
     already exists on disk, leave it alone** (skip recompiling the library
     entirely — the whole point); **if it doesn't, call `build(configPath,
     cachePath, progressLog)` to produce it first** — then, either way,
     append `cachePath` to `config.libraryDbs`, so the ordinary
     attach-and-query machinery from (2) (`SymbolDatabase::attachLibraryDbs`,
     wired in via `ProjectCompiler::loadProject`) picks it up with zero
     changes of its own — literally subsuming (2) exactly as this
     section's own original text predicted, not just conceptually.
   - Called from two places: inside `build()` itself, right after parsing
     `configPath`'s own config and before compiling it (so a library being
     built can itself transitively depend on further lazily-cached
     libraries — mutual recursion between `build()` and
     `resolveLibraryDbSources`, each calling the other, terminates as long
     as the dependency graph has no cycle); and from
     `ProjectRegistry::loadAndCache` (`src/lsp/project_registry.cpp`), right
     after parsing a live project's own config and before
     `ProjectCompiler::loadProject` — the one place that actually serves
     the live server.
   - **Deliberately not fixed, disclosed on `resolveLibraryDbSources`
     itself:** no cycle detection between `libraryDbSources` chains (config
     A's source building config B, whose own source points back at A) —
     would recurse until the stack overflows. A real gap, but a
     contrived one to hit by accident (needs two or more separately
     authored configs coordinated into a cycle), unlike `FilelistParser`'s
     existing `-f`/`-F` cycle guard (trivial to hit by accident with a
     single self-referencing file) which earned its own explicit
     active-recursion-stack check for exactly that reason.

   **Verification:** `tests/unit/compiler/test_filelist_parser.cpp` and
   `tests/unit/lsp/test_project_manifest_parser.cpp` cover the new
   two-argument switch / object-array config-parsing surface (including
   malformed-shape errors: one argument short, not an object, missing
   `config`/`cache`); `tests/unit/lsp/test_library_db_builder.cpp` covers
   `resolveLibraryDbSources` directly — building a missing cache and
   appending it to `libraryDbs`, **reusing an already-existing cache
   without rebuilding it** (proven by pre-populating the cache with a
   *different* symbol than what an actual rebuild would produce, then
   confirming it's untouched — the sharpest test of "skip the rebuild,
   don't silently overwrite"), a failing nested build propagating as a
   thrown `std::runtime_error`, and `build()`'s own transitive resolution
   (a "top" library's config depends on a lazily-cached "nested" library,
   proving the nested cache gets built as a side effect of building top —
   checked via nested-cache.db's own fresh connection, *not* by querying
   top.db for the nested symbol, since `ATTACH` is a live, per-connection
   relationship that never persists into the output file itself; an
   earlier draft of this test asserted the opposite and correctly failed,
   catching its own wrong expectation before it shipped). A new
   `tests/unit/lsp/test_project_registry.cpp` case exercises the real
   live-server entry point end to end: `configFor` on a project whose
   manifest has a `libraryDbSources` entry with no cache yet lazily builds
   it, and the library's symbols are visible afterward via the registry's
   own `SymbolDatabase`. No Emacs functional test, same reasoning as pieces
   1/2.

   **A real bug found only by live-testing against an actual running
   server** (this project's own "empirical verification, not
   grammar/code-reading" discipline, per §6.13 — every unit test above
   passed throughout): `sqlite3_open` (inside `Database`'s constructor)
   does not create missing parent directories, and `build()`'s try/catch
   originally wrapped only config parsing, not the `Database db(outputPath)`
   call right after. Every unit test's `cachePath` happened to sit in a
   directory already created by writing some other fixture file there
   first, so this never surfaced in dozens of passing test cases — it only
   showed up hand-driving a real server session against a `cachePath` whose
   own directory (deliberately modeled on `docs/usage.md`'s own
   `"/var/cache/svlsp/uvm-1.2.db"` example) didn't exist yet. Worse than a
   normal crash: `resolveLibraryDbSources` is reached from
   `ProjectRegistry::loadAndCache`, itself reached from a plain
   `textDocument/didOpen` — a *notification*, not a request — and
   lsp-framework's own dispatcher (`messagehandler.cpp`) silently drops any
   exception escaping a notification handler (there's no response to
   attach an error to), so the failure produced no error, no log line, no
   crash — just a file that silently never got its diagnostics/symbols
   published, and (per the same missing try/catch) would have made
   `--build-db` itself `std::terminate()` the whole process outright for
   the same not-yet-existing-directory case, since `main.cpp`'s own
   `buildDb()` wrapper has no try/catch of its own either and assumes
   `build()` never throws. Fixed by widening `build()`'s try/catch to its
   entire body (restoring its documented "never throws" contract for every
   failure mode, not just config parsing) and calling
   `std::filesystem::create_directories` on `outputPath`'s parent directory
   first (guarded against an empty parent path — a bare `"out.db"` with no
   directory component at all — which `create_directories` throws on
   rather than treating as a no-op). Two new regression tests added:
   `outputPath` in a not-yet-existing directory succeeds, and a
   *different* still-failing case (a directory sitting where the output
   file needs to go) still fails closed via `Result{ok=false}` rather than
   an escaping exception, proving the widened try/catch actually works and
   isn't just testing the one specific bug found.

**Deliberately out of scope for this design**, per explicit direction to
start with attach-and-query rather than the real merge: any id-remapping or
moniker-key merge machinery. This is entirely the "keep DBs separate,
`UNION` at query time" shape, modeled on clangd's `MergedIndex` — not LSIF/
SCIP's moniker-linked physical merge.

**Open question, deliberately unresolved (per explicit direction):** how —
or whether — to detect that a prebuilt/cached library DB has gone stale
relative to its own source files (the library upgraded in place without
rebuilding the DB; a `cachePath` populated once and never revisited even
though `configPath`'s file list changed underneath it). Candidate shapes,
none chosen:
- Per-file content-hash comparison (reusing
  `CompilationController::hashContent`, already used for per-file
  incremental recompilation) between the attached DB's own recorded file
  hashes and the library's current on-disk files — accurate, but requires
  re-resolving and re-reading every library file just to *check*
  staleness, partly defeating the point of skipping the compile.
- A single fingerprint stored as metadata in the DB itself at build time
  (e.g. a hash of the resolved file list plus each file's mtime, or of
  `configPath`'s own content) — cheaper to check, but coarser (a
  metadata-only staleness check, not per-symbol).
- Trust an explicit version/pin the user manages themselves (e.g.
  `cachePath` already encodes a library version in its filename, as in the
  examples above — `uvm-1.2.db`) and never auto-detect staleness at all —
  simplest, matches how most vendored-dependency/lockfile workflows
  already work, but silently serves stale data if someone overwrites a
  library in place without renaming it.
- Some combination — e.g. trust by default, but offer an explicit
  `svlsp --build-db ... --force` (or a separate `--check-stale` mode) for
  whoever owns the cache to run deliberately, rather than `svlsp` ever
  doing this automatically on the hot path.

**Functional/unit tests for piece 1 (implemented):**
`tests/unit/lsp/test_library_db_builder.cpp` — builds from a real `.f`
filelist and reopens the resulting DB file with a *fresh* `Database`/
`SymbolDatabase` connection (not the same in-process objects that built it)
to prove it's a genuinely persistent, independently-readable file, not just
an in-memory result; the same from a `.svlsp.json` manifest; a project with
a real unresolved-instantiation diagnostic to prove `diagnosticCount` counts
project-wide, not just per-file; and a bad config path failing closed
without creating the output DB file at all. No Emacs functional test — this
is a CLI-only mode with no LSP-protocol surface, so it doesn't fit this
project's usual "unit + Emacs" pairing (matching how `FilelistParser` itself
is unit-tested only); instead verified directly by hand against the real
`multifile_project` integration fixture (see piece 1's own writeup above).

**Functional/unit tests for piece 2 (implemented):** see the "Verification"
paragraph under piece 2's own writeup above — config-parsing coverage in
`test_filelist_parser.cpp`/`test_project_manifest_parser.cpp`, and
attach-and-query coverage (all four extended queries, wildcard-import
reach-through, dedup, multiple attached DBs, and an end-to-end
`ProjectCompiler::loadProject` test) in the new
`tests/unit/db/test_symbol_database_library_attach.cpp`.

**Functional/unit tests for piece 3 (implemented):** see the
"Verification" paragraph under piece 3's own writeup above.

---

### 6.20 External Read-Only Database Access (structural analysis by non-editor tools)

**Status:** not started. Added to the plan 2026-09-11, per explicit user
direction.

**Why this is needed:** every query surface this project has built so far
(hover, definition, completion, document/workspace symbols, §6.19's own
attach-and-query library DBs) is reached *through* the LSP protocol, i.e.
through `svlsp` itself acting as the query engine on behalf of one editor
session. There is no way today for an unrelated external program — a
dependency-graph visualizer, a custom lint/metrics script, a dashboard, an
ad hoc `SELECT` for "who calls this function" — to get at the already-compiled
structural data (`symbols`, `instantiations`, `imports`, `diagnostics`; schema
v7, `src/db/schema.h`, documented in `handoff.md`'s "Schema v7" section) without
reimplementing SystemVerilog parsing itself. `svlsp` already does the one
expensive part (parse + resolve); this section is about letting other tools
read the result, not about building any new analysis feature inside `svlsp`
itself.

**A version of this already exists, but only for library DBs, not live
projects:** `svlsp --build-db` (§6.19 piece 1) already produces a real,
standalone, file-backed SQLite database — and because it's a plain file on
disk, it is *already* externally queryable today with any SQLite client
(`sqlite3` CLI, Python's `sqlite3`/`pandas.read_sql`, DB Browser for SQLite,
...) with zero new `svlsp` code. The schema is exactly what `src/db/schema.h`
and `handoff.md`'s "Schema v7" section already document. So: for
rarely-changing library code compiled once via `--build-db`, "secondary
access for external programs" is a solved problem already — this section
should say so explicitly rather than reinvent it, and any docs written for
this feature should lead with "you can already do this for a `--build-db`
output today."

**The actual gap is the live, per-project DB.** `CompilationController`/
`LanguageServer` open their `SymbolDatabase` against `":memory:"`
(`src/lsp/server.h:35`, already flagged as a known gap and as one of §6.5's
own open questions — "in-memory for now; file path in Phase 6"), so the
database backing an actively-edited project literally does not exist as a
file an external process could open. This section is the "make the live
project's DB itself externally readable" half of that same open question,
not a separate mechanism.

**What is missing:**
1. **Make the live DB file-backed.** Subsumes §6.5's own file-backed-DB open
   question (this section doesn't re-litigate that one's own
   restart-persistence motivation — see §6.5 for the cache-invalidation
   tradeoffs of reusing it across restarts; here the file just needs to
   *exist* while the server runs, restart-persistence is a nice-but-separate
   side effect). Needs a real path, not `":memory:"` — candidate default: a
   `.svlsp/<hash-or-name>.db` next to the discovered project config
   (`ProjectRegistry`'s own discovery root), overridable via a new
   `initializationOptions.svlsp.dbPath`, same absent-means-default pattern
   `explicitProjectConfigPath`/§6.11's `fuzzyCompletion`/§6.12's `debounceMs`
   already establish.
2. **Open the file in SQLite WAL journal mode** (`PRAGMA journal_mode=WAL`,
   set once via `Database`'s constructor or an `initSchema()`-time pragma).
   WAL is exactly the mode this use case needs: it lets one writer (`svlsp`
   itself, compiling in the background per §6.8) and arbitrarily many readers
   (external tools) operate on the same file concurrently without blocking
   each other — the default rollback-journal mode does not give this, and
   without it a reader opening the file mid-write could see `SQLITE_BUSY` or
   block `svlsp`'s own writes. An external tool should open its own
   connection **read-only** (SQLite's URI-filename `?mode=ro`, or the
   language binding's own read-only flag) so it can never accidentally
   corrupt `svlsp`'s live DB or race its own writes — `svlsp` itself remains
   the only writer, always.
3. **Treat the schema itself as the public contract**, versioned exactly the
   way it already is internally: `db::SCHEMA_VERSION` (currently 7,
   `src/db/schema.h`) plus the existing migration chain already give external
   consumers a way to check "do I understand this file's shape" before
   querying it, the same way `LibraryDbBuilder`'s own version check (the
   `library_build_info` feature, `handoff.md` 2026-09-08) already does this
   *within* `svlsp` for a different purpose (detecting a stale per-file
   cache). No new versioning mechanism needed — just documenting that this
   existing one is now also an external-facing promise, not purely an
   internal implementation detail.
4. **Document the schema for external consumers** (a new `docs/db-schema.md`
   or an extended `docs/usage.md` section) — table-by-table, in the same
   shape `handoff.md`'s "Schema v7" section already has internally, plus a
   handful of worked example queries (e.g. "every unresolved instantiation,"
   "every class and its parent," "everything a given file imports") so a
   consumer doesn't have to reverse-engineer the schema from `src/db/schema.h`
   and the query methods in `src/db/symbol_database.h/.cpp`.

**Deliberately not proposed here:** a bespoke query API, HTTP/RPC server, or
JSON export layer. SQLite itself is already a mature, widely-supported,
zero-install-cost query interface (every mainstream language has a driver),
and this project's own working style favors reusing an existing, boring
mechanism over building a new bespoke one (§6.19's own "don't invent the
merge mechanism, model on clangd" precedent) — a new API surface would also
need its own versioning/compatibility story that plain "open this file with
SQLite" already gets for free. Revisit only if a concrete use case shows the
raw schema genuinely isn't sufficient (e.g. a consumer needing to be notified
of *changes* rather than polling, which this design doesn't address at all —
external tools would re-query on their own schedule, no push mechanism from
`svlsp` is in scope here).

**No new exposure of anything not already visible:** the DB only contains
structural facts (symbol names/kinds/locations, import/instantiation edges,
diagnostics) derivable from the project's own source tree, which anyone with
read access to the DB file already has filesystem read access to by
construction (it has to sit somewhere the `svlsp` process itself can write,
readable by the same user). Not a new trust boundary, just a new, more
convenient way to read data that was already on disk in source form.

**Open question, deliberately unresolved:** whether raw table access is
enough long-term, or whether a thin layer of read-only SQL views
(`v_symbols`, `v_dependencies`, ...) should sit in front of the raw
`files`/`symbols`/`diagnostics`/`imports`/`instantiations` tables specifically
to decouple external consumers from internal schema churn — the raw tables
already change shape across schema versions for `svlsp`'s own internal
reasons (v5→v6 added `library_include_dirs`, v6→v7 added
`library_build_info`, neither remotely related to external consumption), and
a view layer could stay stable across some of those changes the way a public
API stays stable while its implementation changes underneath. Not designed
here — start with documenting the raw schema (item 4 above) and revisit once
a real external consumer exists to learn what it actually needs.

**Depends on:** §6.5's file-backed-DB open question being resolved in the
"yes, make it file-backed" direction (items 1-2 above are that same piece of
work, done once and shared by both motivations) — if that's ever decided
against for restart-persistence reasons, this section's item 1 still needs to
happen independently for external access to be possible at all.

### 6.21 Semantic Reference-Resolution Diagnostics (unresolved type/import usage)

**Status:** not started. Added to the plan 2026-09-12, found researching §6.4's
own "why don't import edges need cross-file invalidation" question.

**Why this is needed:** every diagnostic this project has ever produced comes
from exactly two sources — ANTLR parse errors, and `LibraryResolver`'s
"unresolved instantiation of X" check (module/interface/program instantiation
only). Confirmed directly (`grep` across every diagnostic-producing call site
in `src/compiler/` and `src/db/`): there is **no check anywhere that a
declared type reference, an imported symbol, or any other identifier actually
resolves to a real declaration**. Concretely: `b.sv` does
`import a_pkg::SomeClass;` then declares `SomeClass obj;`; `SomeClass` is
later removed from `a_pkg` (in `a.sv`) or never existed at all. Today, `obj`'s
declaration still parses cleanly (`SomeClass` is just an `IDENTIFIER` token to
the grammar), and every consumer **fails closed silently** rather than
reporting anything: `hover.cpp` on `obj` calls `findSymbolsByName` (or, for
the *type* itself, whatever resolves `userTypeName()`'s output), gets zero
rows, and returns `nullptr` (no hover, not an error); dot-completion's
`candidatesForResolvedType` chain (§6.10/§6.13/§6.14/§6.17) already
deliberately "fails closed" the same way — no completions, no diagnostic
either. A user sees nothing telling them `SomeClass` doesn't exist — arguably
worse than a stale error, since there's no signal at all, not even a delayed
one. This is also *why* §6.4's cross-file invalidation deliberately scopes
import edges out: there is currently nothing for invalidation to refresh.

**What is missing:**
- A new check, run as part of `CompilationController::compile`'s existing
  extract step, over exactly the set of declared-type references §6.10's
  `userTypeName()` already identifies (class/interface-typed `Signal`s and
  `Parameter`s — the same narrow slice that already exists for dot-completion
  type resolution, not a new extraction pass): for each non-empty
  `userTypeName()` result, resolve it the same way dot-completion's own
  `qualifiedClassScope()`/`pickBestSymbol()` helpers already do (package-aware,
  cross-file, attached-library-DB-aware per §6.19) and emit a diagnostic if it
  resolves to nothing.
- Deliberately narrow scope for v1 — **not** general expression-level
  identifier resolution (an undeclared variable used inside an arbitrary
  expression, a typo'd method call, ...). That is full semantic/type
  analysis, a materially bigger undertaking than anything this project has
  attempted so far (this is a symbol-table-for-IDE-features tool, not a
  compiler-grade checker) and is explicitly out of scope here. Start with
  "does this declared type name resolve," since the resolution machinery for
  exactly that case is already built and proven (§6.10/§6.17); widen only if
  a real need surfaces.
- **A real prerequisite, not just a nice-to-have:** a per-source diagnostic
  clear/replace mechanism. `SymbolDatabase::replaceDiagnostics` today does an
  unconditional `DELETE FROM diagnostics WHERE file_id=?` before inserting —
  shared, undiscriminated storage that already causes a real bug for
  `LibraryResolver`'s own diagnostics (any unrelated live edit silently wipes
  a file's "unresolved instantiation" diagnostic with nothing to re-add it,
  since `LibraryResolver::resolve` isn't re-run on live edits — found in the
  same research pass as this section). §6.7's own schema sketch (`severity`/
  `source` columns, `source = 'parser'|'lint'`) already anticipates exactly
  this problem for its lint engine. This section, §6.7's lint engine, and
  `LibraryResolver` should all land on **one** shared fix — a `source` column
  plus a `replaceDiagnosticsBySource(fileId, source, diags)` that deletes only
  that source's own prior rows — rather than three independent, ad hoc
  patches; whichever of §6.7/§6.21/the `LibraryResolver` live-edit fix lands
  first should build this shared mechanism, not just its own narrow piece of
  it. Proposed `source` value for this section: `'reference'`.
- **Feeds directly back into §6.4**: once this ships, editing `a.sv` to
  remove `SomeClass` must eventually re-flag `b.sv` — the same
  name-based-diff-and-invalidate shape §6.4 already needs for instantiation
  edges (diff a file's declared-type-name set across a recompile, look up
  every file referencing an affected name, queue each for a forced background
  recompile), just keyed off resolved *reference* names instead of
  *instantiated* ones. If §6.4 is implemented before this section, its own
  design should leave room for a second name-index of this shape rather than
  hardcoding "instantiations only."

**Integration tests (once implemented):**
- A file with `import pkg::RealClass; RealClass obj;` where `pkg` genuinely
  declares `RealClass` — zero diagnostics.
- The same file where `RealClass` has been renamed/removed from `pkg` —
  exactly one diagnostic on the `RealClass obj;` declaration line, naming the
  unresolved type.
- A class declared inside a package, referenced via `import`, resolves
  cleanly (regression guard against §6.17's own package-scoped-resolution
  fix regressing once a diagnostic depends on the same resolution path).
- A class that only exists in an attached library DB (§6.19) resolves cleanly
  — this check must reuse the exact same cross-DB-aware resolution path
  hover/dot-completion already use, not a narrower one that would produce
  false positives for perfectly valid library-backed code.

**Unit tests:** the new resolution-check function directly, against a
`SymbolDatabase` fixture — resolves / doesn't resolve / resolves via an
attached library DB / resolves via a package-qualified scope.

**Open question, deliberately unresolved:** default severity. A false
positive here is a real risk independent of genuine bugs — e.g. a class
declared in a project file that hasn't been compiled into the DB yet (an
ordering/incomplete-project-config issue, not a real error, the same class of
false-positive risk `LibraryResolver` already guards against with its own
`compiledPaths`/`failedNames` bookkeeping). Defaulting to `Warning` rather
than `Error` (mirroring §6.7's own "an unrecognized value defaults to
Warning, not Error, so a typo doesn't become build-breaking" reasoning) is
the likely right choice, but not decided here — revisit once real usage shows
how often this actually false-positives in practice.

### 6.22 References, Rename, Signature Help (`textDocument/references`/`rename`/`signatureHelp`)

**Status:** implemented 2026-09-13, closing out §3.5/§3.9/§3.10's original
one-line sketches (unconditional-`null` stubs until now).

**Why this needed a real design, not just "wire it up":** unlike hover/
definition/completion, none of these three have any data model to build on
directly. There is no reference-tracking table in this schema — only
declarations (`symbols`) — and no per-parameter data for function/task
calls (only the function/task's own name and return type are recorded, see
`enterFunction_body_declaration`/`enterFunction_prototype` in
`sv_tree_walker.cpp`). Building the "real," fully semantic, scope-aware
version of any of these three would be a substantial new subsystem (general
reference resolution — the same underlying gap §6.21 already documents for
a different reason). Given the size of implementing all three in one pass,
each was scoped to a genuinely useful, honestly-limited v1 built on data
that already exists, with the gap to "real" explicitly disclosed rather than
silently accepted.

**References and rename — a lexical, cross-file text search:**
- New `SymbolDatabase::allFilePaths()` — every path in `files`, no reference
  edges to walk.
- New `symbol_utils.h`/`.cpp`: `findIdentifierOccurrences(text, name)` — a
  lexical scanner (not a re-parse) returning every whole-identifier match of
  `name` in `text`, skipping `//`/`/* */` comments and `"..."` string
  literals (same in-string escape handling the preprocessor's own
  macro-argument scanner already uses) so a same-named identifier inside one
  of those is never (wrongly) reported.
- `ReferencesProvider::getReferences(params, db, docText, textForPath)`:
  resolves the word under the cursor (`wordAtPosition`, same helper hover/
  definition already use), confirms it's a real known symbol
  (`findSymbolsByName` non-empty — fails closed otherwise), then scans every
  file `allFilePaths()` returns via `textForPath` (an injected
  path→text lookup, so this provider needs no knowledge of `DocumentStore`
  or the filesystem itself), filtering out the declaration's own location
  unless `context.includeDeclaration` is set.
- `RenameProvider::getRename` reuses the identical scan (always including
  the declaration, since a rename must cover it) and builds a
  `WorkspaceEdit` with one `TextEdit` per occurrence per file. Validates
  `newName` against IEEE 1800's "simple identifier" grammar
  (`[a-zA-Z_][a-zA-Z0-9_$]*`) and throws `lsp::RequestError` (`InvalidParams`)
  otherwise, per the LSP spec's own requirement that an invalid new name be
  reported as an error response, not a silently-broken edit.
- **Disclosed limitation, not fixed:** this is name-based, not scope-aware —
  two unrelated declarations that happen to share a name (two classes both
  named `Packet` in different files, say) are indistinguishable from here
  and both get included/renamed. A real fix needs the same semantic
  reference-resolution machinery §6.21 already scopes out as a separate,
  larger effort.
- Both providers take an injected `textForPath` callback rather than reading
  `DocumentStore`/disk directly, for testability (a unit test backs it with
  a plain `std::map`) and reuse: `LanguageServer` passes its own
  `currentTextFor` (m_store if open, else disk — the same helper
  `forceRecompileAndPublish` already uses for §6.4, extracted for this
  second use).

**Signature help — scoped to module/interface/program instantiation port
lists only:**
- Reuses existing `Port`-kind symbols (name + direction already recorded via
  `enterAnsi_port_declaration`) — no new extraction needed for this part.
- A local (signature_help.cpp-only) lexical scan: `findEnclosingParen` walks
  backward from the cursor with a paren-depth counter to find the argument
  list it's inside; `parseInstantiationHeader` then reads the two
  identifiers immediately before that `(` (`<TypeName> <InstanceName> (`).
  The resolved type name is looked up via `findSymbolsByName`, filtered to
  Module/Interface/Program kind, disambiguated by the existing
  `pickBestSymbol` (same-file-first) — then its ports come from
  `findSymbolsInScope` on its own qualified scope, sorted by (line, col) to
  approximate declaration order.
- Active-parameter tracking counts top-level commas between the paren and
  the cursor for a plain positional index, but also recognizes a named port
  connection (`.portName(` — ubiquitous in real SV verification code) at the
  start of the current argument and resolves that to the matching port's
  index by name instead, when present.
- **A real subtlety found implementing this:** a named connection's own
  parens (`.portName(`) must NOT be treated as a nested call by
  `findEnclosingParen` — a cursor positioned to type the connected signal
  (right after `.a(`) needs the *outer* instantiation's own paren, not this
  one. `isNamedConnectionParen` distinguishes this shape (`.identifier(`
  where the `.` itself starts a fresh argument — preceded by `(`, `,`, or
  start-of-text) from a genuine dotted method call (`obj.method(`, where an
  identifier rather than `(`/`,` precedes the `.`) so the latter is left
  alone (and simply fails to resolve later, since no per-parameter data
  exists for arbitrary calls either way).
- **Function/task calls are explicitly NOT supported** — the reason this
  section opened with: there is no per-parameter data for them anywhere in
  this schema to build a signature from. See the "Follow-up, deliberately
  not started here" subsection right after this list for the full design
  needed to close this; `SignatureHelpProvider`'s own header comment also
  flags it as the concrete next step if it's ever picked up.
- **Also not attempted:** a parameter-override block between type and
  instance name (`Module #(...) inst (`) — `parseInstantiationHeader` only
  recognizes the plain `TypeName InstanceName (` shape and fails closed
  (returns no signature help) otherwise, a disclosed simplification rather
  than an oversight.

**Follow-up — extending signature help to function/task calls, implemented
2026-09-13.** Arguably the more commonly wanted case than module
instantiation (`my_func(a, b, |` mid-expression is a far more frequent
editing moment than typing a port list), explicitly out of scope for the
pass above for the reason already stated: no per-parameter data exists for
functions/tasks anywhere in this schema, only the function/task's own name
and return type (`enterFunction_body_declaration`/`enterFunction_prototype`/
their task equivalents, `sv_tree_walker.cpp`). What closed this, following
the sketch below essentially as designed:

1. **A new listener pair, `enterTf_port_item`** (grammar rule
   `tf_port_item`, reached via `tf_port_list` from
   `function_body_declaration`/`function_prototype`/`task_body_declaration`/
   `task_prototype`'s own `'(' tf_port_list ')'`) — one call per declared
   parameter, extracting its `port_identifier` (name),
   `tf_port_direction` (direction — defaults to `input` per LRM semantics
   when the grammar's own `tf_port_direction?` is absent, so the listener
   should apply that default explicitly rather than leaving it blank), and
   `data_type_or_implicit` (declared type).
2. **Reuse `ParseRecordKind::Port` for these, not a new kind.** A function/
   task parameter is semantically the same shape a module port already is
   (name + direction + type) — reusing `Port` means
   `SignatureHelpProvider`'s existing `findSymbolsInScope(scope)` →
   filter-to-`Port` → sort-by-`(line,col)` pipeline (already built for
   module ports) works for function/task parameters completely unchanged,
   with zero new query code. The only genuinely new provider-side code is:
   (a) the extraction listener itself, and (b) teaching
   `SignatureHelpProvider` a *second* call-header shape — a function/task
   call has no separate instance name, so `parseInstantiationHeader`'s
   "two identifiers before the paren" reading doesn't apply; only *one*
   identifier (the callee's own name) precedes a plain call's `(`. Try the
   existing two-identifier (module instantiation) shape first, then this
   one-identifier shape, resolving via `findSymbolsByName` filtered to
   Function/Task kind instead of Module/Interface/Program.
3. **A verbatim-source-text gap this needs, confirmed by reading
   `SvTreeWalker::walk` directly (not assumed):** ANTLR's default
   `ctx->getText()` strips inter-token whitespace entirely (`"input logic
   [7:0] a"` becomes `"inputlogic[7:0]a"`) — already true of every existing
   `detail` field populated this way (e.g. a function's own return-type
   text), but never mattered before now since those are only ever shown as
   short, single-token-ish hover text, not a multi-token parameter list a
   person will actually read character-by-character in a signature-help
   popup. `SvTreeWalker::walk` constructs its `antlr4::CommonTokenStream`
   as a purely local variable and never passes it to `SvRecordListener` at
   all today — getting real source spacing (via the token stream's own
   `getText(startToken, stopToken)`, not the parse tree context's) needs
   `SvRecordListener`'s constructor widened to accept the token stream, and
   `walk()` to pass `&tokens` through. This is the one piece of this
   follow-up that touches code outside the listener itself.
4. **Default parameter values** (`('=' expression)?` in `tf_port_item`) are
   worth surfacing in the signature label too (`int width = 8`, not just
   `int width`) — same verbatim-text extraction as (3), capturing the
   `'=' expression` span alongside the type.
5. **No overload handling needed:** SV allows only one declaration per
   function/task name in a given scope, so `SignatureHelp.signatures` stays
   a single-entry array here too, consistent with the module-instantiation
   shape already implemented.
6. **Scope this follow-up to bare, undotted calls only** (`my_func(`,
   including a class method called as `my_func(` from inside its own
   class) — a *dotted* call (`obj.method(`) needs the same chain-resolution
   machinery completion's own `resolveChain`/`resolveMemberSegment`
   (`completion.cpp`) already has for dot-completion, which is a
   meaningfully bigger lift than this section's own scope; treat that as a
   further, separate follow-up, not bundled into this one.

**Implementation notes, found doing the above:**
- `enterTf_port_item` (`src/compiler/sv_tree_walker.cpp`) skips a parameter
  with no `port_identifier` at all rather than crash or mis-tag it — this is
  a real, narrow gap, not a defensive no-op: SV's comma-shorthand for
  sharing one direction/type across several parameters (`function int
  add(input int a, b);`, `b` inheriting `input int` from `a`) is genuinely
  ambiguous in this grammar's own `tf_port_item` rule (`data_type_or_implicit`
  is mandatory, `port_identifier` is not, so a bare trailing `b` parses as an
  unnamed parameter *of type* `b`, not a second `int` parameter named `b` —
  the same category of grammar ambiguity as the already-documented
  `data_type`/`variable_decl_assignment` one). Not fixed here — real
  verification code overwhelmingly spells out each parameter's own
  direction/type explicitly, so this only bites the terser, rarer style.
- The direction extracted per parameter is only surfaced in the label when
  it's not the default `input` (`output`/`inout`/`ref`/`const ref` are kept,
  since those are genuinely informative; showing `input` on every single
  ordinary parameter would just be noise no real tool does).
- `portLabel()` (`signature_help.cpp`) splits a Port's `detail` on a new
  sentinel byte, `PARAM_DEFAULT_VALUE_SEP` (`parse_record.h`, `'\x1F'` —
  never producible by real SV source), to place a default value *after* the
  parameter name (`int width = 8`) while direction/type stay a *prefix*
  (`output int b`) — the same field format module/interface/program ports
  already use, just with an optional suffix appended past the sentinel; a
  plain port's `detail` never contains it, so existing rendering is
  unaffected.
- `SignatureHelpProvider::getSignatureHelp` tries the two-identifier
  (module-instantiation) header shape first, exactly as piece 2 above
  describes; only if that resolves to no Module/Interface/Program does it
  try the new one-identifier call shape. This has one disclosed, accepted
  overlap: `Foo bar(` where `Foo` isn't a real design-unit type but `bar`
  happens to also be a declared function/task elsewhere would then resolve
  against that unrelated function — an inherent consequence of both shapes
  being lexical heuristics over the same ambiguous prefix, not a new
  regression; not guarded against, since doing so would require knowing
  intent this lexical scan doesn't have.
- `parseCallHeader` explicitly fails closed (returns `nullopt`) when a `.`
  precedes the identifier before the paren, rather than resolving a dotted
  call's method name alone — implementing piece 6's "dotted calls are out of
  scope" as a real guard, not just a documented gap, since silently
  resolving by bare method name would risk matching an unrelated same-named
  method on a completely different class.

**Unit tests:** `tests/unit/compiler/test_sv_listener.cpp` (function/task
parameter recorded as Port; non-default direction kept in `detail`; a
two-token direction like `const ref` keeps its real spacing — a regression
guard for the verbatim-token-stream extraction; a default value appended
past the separator; a class method's parameters scoped `Class::method`; a
pure-virtual method's parameters recorded despite having no body);
`tests/unit/lsp/test_signature_help.cpp` (bare function call with 2
differently-directioned parameters; a task call; a class method called bare
from inside its own class; a zero-argument call resolving to an empty
parameter list rather than null; a default value rendered after the name; a
dotted call still fails closed; a call to a non-function/task symbol still
fails closed). Unit suite now at 2070 assertions / 627 test cases, no
regressions.

**Functional tests:** `tests/integration/test_12_signature_help.sh` gained
one new case against a new fixture addition, `rrsh_compute(input int
rrsh_a, input int rrsh_b = 4)` and its call site in `rrsh_caller` (appended
to the end of `fixtures/ref_rename_sighelp.sv` rather than inserted, so the
two pre-existing cases' hardcoded line numbers stay valid) — confirms the
resolved label (`"rrsh_compute(int rrsh_a, int rrsh_b = 4)"`, direction
dropped since both are the default `input`, default value rendered after
the name), parameter count, and positional `activeParameter` all come back
correctly through a real `initialize`/`didOpen`/`signatureHelp` round trip.

**UVM corpus test, added same day:** `tests/uvm_corpus/test_signature_help_uvm.cpp`
(new, registered in `CMakeLists.txt`'s `uvm_corpus_tests` target) — closes
part of the disclosed gap the original §6.22 pass left open (no
`tests/uvm_corpus/test_*_uvm.cpp` coverage for references/rename/
signatureHelp at all). Scoped to the bare-function/task-call half only: the
real UVM-core corpus is a pure class library with no modules/interfaces to
instantiate anywhere in it, so the module/interface/program-port-list half
of `SignatureHelpProvider` has no real fixture to exercise here. Three cases
against real call sites in `uvm_component::do_execute_op`
(`base/uvm_component.svh`, an out-of-class method body): a bare call to a
single-`input`-parameter method (`get_child(name)`, direction dropped from
the label since `input` is the default); a bare call to a `ref`-direction
parameter (`get_next_child(name)`, direction kept in the label); and a
dotted call (`m_children[c].set_domain(domain)`) confirmed still `nullptr`
against a real method with real recorded parameters, not just a synthetic
unit-test fixture. Both `get_child`/`get_next_child` are declared twice in
this real file (an `extern function ...;` prototype inside the class body,
plus the actual out-of-class `function ... uvm_component::name(...)`
definition) — `pickBestSymbol`'s same-file/earliest-line tie-break resolves
to the in-class prototype's own scope both times, empirically confirmed
rather than assumed (all three cases passed on the first real-corpus run).
Full `uvm_corpus_tests` suite: 505 assertions / 14 test cases, no
regressions (up from the pre-existing baseline's 496/11).

**Unit tests (§6.22's original three, unchanged):** `tests/unit/lsp/test_references.cpp` (cursor not on an
identifier; unknown identifier; excludes/includes the declaration;
cross-file; comment/string exclusion; a file with unavailable text is
skipped, not a crash); `tests/unit/lsp/test_rename.cpp` (the same shape,
plus invalid-identifier rejection via `CHECK_THROWS_AS`, and a
multi-file `WorkspaceEdit`); `tests/unit/lsp/test_signature_help.cpp`
(no enclosing parens; unknown type name; a real port list; positional
active-parameter tracking; named-connection active-parameter tracking
overriding the positional count; works for `interface` too; explicitly
confirms `null` for a function call). Unit suite now at 2029 assertions /
614 test cases, no regressions.

**Functional tests:** `tests/integration/test_07_references.sh`,
`test_11_rename.sh`, and `test_12_signature_help.sh` — these three files
already existed as the original "always null" stub tests and were rewritten
in place (not given new numbers) against a new fixture,
`fixtures/ref_rename_sighelp.sv`. Emacs integration suite now 204/204 across
39 files, no regressions.

**A real fixture-naming bug caught by the full suite, not the isolated
three-file run:** the fixture's first draft named its module `adder` —
already declared by `examples/module_basic.sv`, opened by several earlier,
unrelated tests. Since this daemon session shares one `svlsp` process and
one growing in-memory DB across every `test_*.sh` file, never purged on
`didClose` (a documented risk in this handoff/plan already, "Emacs test
infrastructure" section), `findSymbolsInScope("adder")` silently unioned
*both* files' ports together once `module_basic.sv` had been opened by an
earlier test — a name collision, not a provider bug. Running the new tests
in isolation (just these three files) never surfaces this, since
`module_basic.sv` is never opened in that narrower run — only the full
39-file suite does. Fixed by prefixing every declared name in the new
fixture with `rrsh_`, matching this repo's own established convention for
exactly this reason (`gapb_`, `diagvis_`, `svlsp_test_` prefixes elsewhere).
Worth remembering for any future fixture: check `tests/integration/
fixtures/**` and `examples/**` for a name collision, and always run the
*full* suite at least once before trusting an isolated few-file run — see
handoff.md's own "Emacs test infrastructure" section for the same caution.

**Follow-up, not started: UVM corpus tests for `references`/`rename`.**
`test_signature_help_uvm.cpp` (added 2026-09-13, see this section's own
signature-help follow-up above) closed this gap for signatureHelp's
bare-function/task-call half; `references`/`rename` still have no
`tests/uvm_corpus/test_*_uvm.cpp` coverage at all — the original §6.22 pass
only had unit tests (synthetic, hand-built DB rows) and Emacs functional
tests (a small dedicated fixture), neither of which exercises the lexical
scan against the real ~140-file corpus's actual scale and naming. Candidate
cases, following `test_signature_help_uvm.cpp`'s own pattern of picking
real, stable call/declaration sites in `base/uvm_component.svh`:
- **References:** a `getReferences` call on a widely-used identifier (e.g.
  `get_name` — declared once, called from many places across the corpus)
  confirms every real cross-file occurrence comes back, not just a
  same-file subset — the kind of scale bug a small hand-built fixture can't
  surface.
- **A real same-named-but-unrelated-declaration case, if one can be found**
  (or synthesized alongside real corpus text) — directly exercises this
  provider's own disclosed "name-based, not scope-aware" limitation
  (plan.md, this section, above) against real code, the way
  `test_signature_help_uvm.cpp`'s dotted-call case did for signatureHelp's
  own disclosed limitation.
- **Rename:** a `getRename` call on a real declaration, confirming the
  `WorkspaceEdit` covers every real file the identifier appears in (a
  genuine multi-file edit at corpus scale, not the 2-3-file synthetic case
  the unit test already covers) — read-only (never actually apply the edit
  to the checked-out corpus on disk).
- Register the new file in `CMakeLists.txt`'s `uvm_corpus_tests` target,
  same as `test_signature_help_uvm.cpp`; verify with an isolated run first,
  then the full `uvm_corpus_tests` binary to catch any interaction with the
  other provider tests sharing the same corpus DB.

---

### 6.23 Missing-Required-Argument Diagnostic (function/task calls)

**Status:** implemented 2026-09-17, following this section's own sketch
essentially as designed. See handoff.md's 2026-09-17 entry for the full
writeup. **One follow-up not finished: a full UVM-corpus false-positive
probe was started but interrupted — see handoff.md's "PICK UP HERE NEXT
TIME" note before treating this as fully verified.**

**Motivation:** §6.22's follow-up (above) added, for the first time, real
per-parameter data for function/task parameters — name, direction, declared
type, and whether a default value is present (`enterTf_port_item`,
`sv_tree_walker.cpp`; a default's presence is currently encoded as a
`PARAM_DEFAULT_VALUE_SEP`-delimited suffix on `Port::detail`, `parse_record.h`).
Nothing in this codebase currently checks a real call site against that
data: `my_func(a)` against `function int my_func(int a, int b);` (no default
on `b`) compiles with zero diagnostics today, even though every real
simulator rejects it. This closes that gap.

**Grammar, confirmed by reading `grammar/Sv.g4` directly (not assumed):**
`tf_call: ps_or_hierarchical_tf_identifier attribute_instance* ('(' list_of_arguments ')')?`
— note the parens themselves are optional (a task call with nothing but
defaulted parameters can be written with no parens at all, e.g. `my_task;`).
`list_of_arguments` supports **both** forms real SV allows for a plain call,
not just positional:
```
list_of_arguments :
      expression? (',' expression?)* (',' '.' IDENTIFIER '(' expression? ')')*
    | '.' IDENTIFIER '(' expression? ')' (',' '.' IDENTIFIER '(' expression? ')')*
;
```
Two things worth calling out that an initial, un-verified assumption would
have gotten wrong:
- **Named argument association (`my_func(.b(2), .a(1))`) is legal for plain
  function/task calls in SystemVerilog, not just module port connections.**
  This means the diagnostic needs the *same* named-vs-positional argument
  matching `computeActiveParam`/`isNamedConnectionParen`
  (`signature_help.cpp`) already implements for module instantiations —
  reuse that logic (or extract it to a shared helper) rather than
  re-inventing it.
- **A positional slot can be elided** (`expression?` between commas, e.g.
  `foo(a, , c)`) to explicitly skip a defaulted parameter while still
  supplying later ones by position — this is a real, legal SV construct
  the "missing argument" check must treat as "parameter 2 intentionally
  uses its default," not as a syntax error or an unrelated omission.
- `ps_or_hierarchical_tf_identifier` reaches this same `tf_call` rule for
  **both** bare and dotted calls (`obj.method(...)` is a hierarchical
  identifier, not a separate grammar alternative) — distinguishing them is
  a property of the extracted identifier text (single segment vs.
  containing a `.`), not the grammar shape. Scope this first pass to the
  single-segment (bare, undotted) case only, matching §6.22 follow-up's own
  disclosed scope limit — a dotted call needs the same chain-resolution
  machinery already deferred there.

**Design, following §6.21's own architecture note for the same reason:**
a call site's callee may be declared *later* in the same file (SV classes
allow calling a sibling method declared further down the class body with no
forward declaration, unlike C++) or in another file entirely — so this
cannot be checked inline during the single-file AST walk the way,
say, `enterTf_port_item` populates a record for its own enclosing scope.
It needs a real post-parse resolution step over the completed
`SymbolDatabase`, the same shape §6.21 already scopes out for unresolved
type/import references:
1. A new listener, `enterTf_call`, extracts one `CallRecord` per bare
   `tf_call` site: callee name, an ordered list of argument "slots" (each
   either a supplied positional expression, an elided positional slot, or
   a named `.identifier(...)` connection), plus line/file — the same
   shape `InstantiationRecord` already uses for module instantiations,
   sibling to it rather than folded into it (a function/task call's own
   argument-matching rules, per the two bullets above, are meaningfully
   different from a module port connection's).
2. A new resolution pass (`CompilationController`, run after symbols are
   persisted, mirroring §6.21's own placement) resolves each `CallRecord`'s
   callee name against `SymbolDatabase` (Function/Task kind only,
   `pickBestSymbol` for the same-name-collision case — see this section's
   own disclosed references/rename limitation above for why that tie-break
   is already an accepted, pre-existing simplification in this codebase),
   then reads that symbol's own Port list as `SignatureHelpProvider`
   already does. For each declared parameter, checks whether it received a
   value (by position, honoring elided slots, or by name); one without a
   default and without either counts as a real diagnostic: `error: missing
   required argument 'b' in call to 'my_func'` at the call site's own
   location, persisted through the same `diagnostics` table/
   `replaceDiagnostics` flow every other diagnostic already uses.

**Deliberately out of scope for a first pass:**
- Dotted calls (`obj.method(...)`) — see above.
- `system_tf_call` (`$display`, `$cast`, ...) — variadic/built-in, never
  has `tf_port_item`-recorded parameters to check against.
- A callee name that doesn't resolve to any known Function/Task at all
  (typo, unresolved import, or genuinely routed to a different grammar
  rule entirely — `new(...)`, `randomize()`, a `let` call) is silently
  skipped, not flagged; whether *that* name should itself produce a
  diagnostic is §6.21's own, already-scoped-out concern, not this one's.
- **Worth testing explicitly, not assumed:** a DPI-imported function/task
  (`import "DPI-C" function ...`) reduces to the same `function_prototype`/
  `task_prototype` rules §6.16 already wired up, so it *would* have real
  Port data recorded — confirm this diagnostic behaves sensibly (or is
  deliberately excluded, if DPI's own C-side calling convention makes the
  check meaningless) rather than assuming either way.
- **Not a real risk, but worth recording why:** a virtual method override
  cannot legally redeclare a different parameter list than the method it
  overrides (an LRM constraint), so resolving a bare/self call inside a
  base class's own method body against that method's *statically* declared
  parameter list is always correct — no runtime-dispatch ambiguity to
  worry about here, unlike, say, hover/definition's own same-name-in-
  different-files ambiguity.

**Unit tests, once implemented:** a call missing a required trailing
argument; a call using an elided positional slot to correctly skip a
defaulted parameter (no diagnostic); a call using named association to
supply an otherwise-missing required parameter (no diagnostic); a call to
an unresolved name (no diagnostic, not this check's job); a dotted call
(no diagnostic, out of scope); a DPI-imported call, to settle the "worth
testing" item above one way or the other.

---

### 6.24 Audit Compiler Warnings from `tools/build.sh`

**Status:** not started.

**Motivation:** `-Wall -Wextra -Wpedantic` are already enabled for every
first-party target (`svlsp_options`, `CMakeLists.txt`) — the ANTLR4-generated
parser/lexer and the SQLite amalgamation are the only two exemptions,
deliberately built with `-w` since they're machine-generated/vendored code
this project doesn't own. Nobody has actually sat down and reviewed what
those flags currently surface across `src/`/`tests/` as its own task — a
`tools/build.sh` from-scratch build's warning output has never been
captured and gone through line by line.

**Plan:**
1. Run `tools/build.sh debug` and `tools/build.sh release` from scratch
   (each wipes its own `build/<preset>` first, so this is a genuinely clean
   compile of every first-party file), capturing full compiler output to a
   log rather than letting it scroll past.
2. Grep both logs for `warning:` and review every *distinct* one (same
   warning at the same source location across debug/release only counts
   once) — real bugs get fixed outright; a warning that's a deliberate,
   understood pattern (if any turn out to be) gets a narrow, explained
   suppression (a targeted pragma plus a comment saying why, not a blanket
   flag downgrade) rather than being silently left alone.
3. Record the outcome here or in handoff.md either way — "reviewed, N
   warnings found, M fixed, K deliberately suppressed with reasons" or
   "reviewed, zero warnings" — so this doesn't quietly need re-doing from
   scratch next time someone wonders whether the build is clean.
4. **Open question, don't decide preemptively:** once the backlog (if any)
   is actually clean, consider adding `-Werror` (at minimum to debug
   builds, where ASan/UBSan already make the build strict about other
   classes of bug) to keep new warnings from silently accumulating again —
   flagged here as something to weigh once there's a real "clean" baseline
   to protect, not decided now.

---

### 6.25 Investigate Signature Help for Macro Invocations (`` `uvm_info(...) ``-style)

**Status:** not started — investigation only, not committed to implementation.
Implementation of macro signature help is tracked as part A of §6.29; this
investigation is its first step.

**Motivation:** §6.22's follow-up extended signature help to bare function/task
calls; macro invocations (`` `uvm_info(ID, MSG, VERBOSITY) ``,
`` `uvm_object_utils(T) ``, and the whole UVM macro surface that real users of
this LSP actually type constantly) currently get none at all, and it's not
obvious this is a small extension of the existing feature rather than a
mostly-separate one — worth an explicit investigation pass before committing
to a design.

**Confirmed by reading the code directly, not assumed:**
- Macro definitions are resolved entirely inside `SvPreprocessor`
  (`src/compiler/sv_preprocessor.cpp`) as pure text substitution, *before*
  `SvTreeWalker`/ANTLR ever see the token stream — a macro invocation site is
  gone (fully expanded) by the time the parse tree signature help already
  walks even exists. This is a materially different pipeline stage than the
  bare-function-call case, which resolves against `Port` records the AST
  walker itself populated.
- `MacroRecord` (`src/compiler/sv_preprocessor.h`) currently stores only
  `name`, `body` (raw macro-body text), and a definition `line` — **no
  parameter names or count at all**, even though `parseMacroDefinition`
  (`sv_preprocessor.cpp`) clearly parses a parameter list internally to do
  the substitution; that data just isn't retained on `MacroRecord` once
  substitution is done.
- `ParseRecordKind::Macro` already exists (`src/compiler/parse_record.h`) but
  is dead in practice: `grep -rn ParseRecordKind::Macro src/` finds it used
  only in `symbol_database.cpp`'s kind-to-string mapping — nothing anywhere
  actually constructs a `Macro`-kind `ParseRecord`, and `PreprocessorResult::
  macros` (populated per-call) has no consumer in `CompilationController` at
  all. Macros are, today, entirely invisible to `SymbolDatabase` — no macro
  can currently be hovered, go-to-definition'd, or found by workspace symbol
  search either, not just signature help specifically.

**What an investigation pass should actually settle, not implement outright:**
1. Whether `MacroRecord` should carry a real parameter list (names, and
   whether each is variadic/has a default per `` `define FOO(a, b=1) ``-style
   defaults, which SV macros do support) — likely requires widening
   `parseMacroDefinition`'s already-parsed-and-discarded parameter list to
   survive onto the record, the same shape of gap §6.22's follow-up closed
   for `tf_port_item` (parsed data existed, just wasn't retained past its
   immediate use).
2. Whether macros belong in `SymbolDatabase` as real `Macro`-kind
   `ParseRecord`s at all (which would also incidentally unlock macro
   hover/definition/workspace-symbol, arguably a bigger win than signature
   help alone), versus a narrower, preprocessor-local lookup table
   `SignatureHelpProvider` queries directly without going through the DB —
   these have different persistence/invalidation implications (a macro
   redefinition on a later `` `include ``d file, conditional-compilation
   branches changing which definition is "active," etc.) worth weighing
   before picking either.
3. Whether a macro invocation's *call site* is even visible anywhere once
   `CompilerDirectiveStripper`/`SvPreprocessor` have run — since the whole
   point of the preprocessor pass is to erase the invocation by expanding it
   in place, signature help's usual "find the enclosing open-paren at the
   cursor" scan (as `SignatureHelpProvider` does today for both its existing
   shapes) would need to run against something closer to the *original*
   source text/token positions, not the post-expansion buffer the AST walker
   consumes — confirm whether the existing `sourceMap`
   (`PreprocessorResult::sourceMap`, already used to map expanded-line
   positions back to original source for diagnostics) is sufficient for this
   or whether it's the wrong tool for a mid-line cursor position.
4. Scope check against real usage: UVM's own macros are overwhelmingly
   function-like with positional (not named) arguments and no defaults in
   practice — confirm whether SV even allows named-argument or default-value
   macro invocation syntax at all (unlike function/task calls, which
   §6.23 already had to handle named connections and defaults for) before
   assuming the full §6.22/§6.23 argument-matching machinery is needed here
   too; a much simpler positional-only implementation may be all real usage
   ever needs.

**Deliberately not decided here:** whether this is worth building at all
once the above is understood — record the findings (here or in handoff.md)
either way, including "investigated, not worth it because X" as a valid
outcome, rather than treating investigation as an implicit commitment to
ship it.

---

### 6.26 Scope/Type-Aware Method-Call Resolution (`Class::`, bare in-class calls, inheritance)

**Status:** implemented 2026-09-20, following this section's own design
sketch below essentially as planned. Re-running §6.23's own UVM-corpus probe
after this landed: **`PROBE-TOTAL` dropped from 349 to 0** — every false
positive this section's motivation describes is gone. Full unit suite
(2148 assertions/661 cases, 6 new [db][symbol-db][chain] cases for
`baseClassChain`/`resolveMethod`, 4 new [call-args] cases, 1 new
[completion][dot][chain] case, 1 new [signature_help] case), full UVM-corpus
suite (505 assertions/14 cases, unchanged from baseline — no regressions),
and full Emacs functional suite (207/207) all re-run clean. See handoff.md's
2026-09-20 entry for the complete writeup. Not yet committed — bundled with
§6.23 into a single commit per the user's own instruction; only commit when
asked.

**Motivation:** surfaced directly by §6.23's own UVM-corpus verification
step (see handoff.md's 2026-09-20 entry) — running the real §6.23 checker
against the ~170-file UVM corpus produced 349 false "missing required
argument" diagnostics, none caused by the comma-shorthand ambiguity gap
§6.23 itself had flagged as the prime suspect. Root-caused instead to two
more fundamental gaps that predate §6.23 and affect other features too: this
codebase resolves a `Class::method(...)` call, and a bare call inside a
class method body, by name alone across the *entire* database, with no
awareness of which class was actually named (for `Class::`) or which class
the call site is lexically inside (for a bare call), and no notion of
inheritance at all. A survey done as part of this same investigation (see
handoff.md) confirmed the scope of the gap precisely — recorded here so the
fix doesn't have to re-derive it:

- **Dotted calls (`obj.method(...)`)** already have *real*, declared-type-scoped
  resolution — `resolveFirstSegment`/`resolveMemberSegment`/
  `candidatesForResolvedType` (`src/lsp/completion.cpp`) resolve `obj`'s
  declared type via `findSymbolsVisibleAt`, then scope the member lookup to
  that exact class via `qualifiedClassScope` → `db.findSymbolsInScope`. This
  is the one call shape that is *not* broken. Its own limitation, worth
  fixing in the same pass since the underlying data model is the same:
  inheritance is followed exactly **one hop**, and only for the literal
  `super` keyword (`resolveFirstSegment`, `completion.cpp`) — a method
  declared two or more `extends` levels up an ordinary object handle's class
  hierarchy is invisible to completion today, not just to `super.`.
- **`Class::method(...)` calls** — `enterTf_call` (`src/compiler/sv_tree_walker.cpp`)
  discards the `Class::`/`pkg::` prefix entirely and records only the bare
  trailing identifier; `parseCallHeader` (`src/lsp/signature_help.cpp`) does
  the same (it only special-cases a `.`-qualified prefix, never a
  `::`-qualified one). Both resolvers then call `findSymbolsByName` across
  the whole DB and disambiguate with a same-file-preferred tie-break
  (`pickBestSymbol`/`pickCallee`) that has no way to prefer "the method
  actually declared on the named class" when the call site isn't in that
  class's own file — which it usually isn't, since `Class::` is precisely
  the syntax used to call a *different* class's static/scoped method. This
  is the majority root cause of the 349 UVM false positives (e.g.
  `type_id::get()`, a zero-arg factory call generated by every single
  `` `uvm_component_utils ``/`` `uvm_object_utils `` invocation, resolving
  against an unrelated `get(KEY_T key)` on some other class entirely).
- **Bare calls inside a class method body** (implicit `this.method(...)`,
  e.g. calling an inherited method with no qualifier) — same flat
  `findSymbolsByName` path, with no use anywhere of `enclosingClassNameAt`
  (the helper `completion.cpp` already uses for `this`/`super`) or any
  notion of the enclosing class's base-class chain. Minority root cause of
  the UVM false positives (one: `do_write(rw)` inside
  `uvm_reg_indirect_data extends uvm_reg`, resolving against an unrelated
  `uvm_resource::do_write(T t, uvm_object accessor)` instead of the real,
  inherited one-argument `do_write`).
- **Hover and go-to-definition** (`src/lsp/hover.cpp`, `src/lsp/definition.cpp`)
  are flatter still — `wordAtPosition` extracts only the bare identifier
  under the cursor, ignoring any `obj.`/`Class::` qualifier context
  entirely, then the same `findSymbolsByName` + same-file tie-break. Not
  this section's primary trigger, but the same fix should improve these for
  free once a real resolution helper exists, and they're listed here so a
  future session doesn't have to re-survey them.
- **No inheritance-edge data exists in the schema at all.** A class's
  parent (`extends`) is recorded only as a single string in that class's own
  `detail` column (`enterClass_declaration`/`enterInterface_class_declaration`,
  `sv_tree_walker.cpp`) — single-inheritance only (an interface class's
  additional parents beyond the first are dropped, already disclosed
  elsewhere), no dedicated table, no transitive closure, and read back in
  exactly one place (`resolveFirstSegment`'s `super` case). There is nothing
  to query for "give me every ancestor of class X" today.

**Design sketch, not yet finalized:**
1. **Make inheritance queryable without a schema migration first, if
   possible** — `symbols.detail` already holds a class's immediate parent
   name; a `SymbolDatabase::baseClassChain(className, curPath)` helper could
   walk it iteratively (`class → detail → class → detail → ...`, stopping on
   an empty `detail` or a cycle guard) entirely by repeated
   `findSymbolsByName`+`pickBestSymbol` calls, no `CREATE TABLE` needed. Only
   fall back to a real schema addition (a `class_id, base_name, ordinal`
   table, needed anyway if interface classes' *multiple* parents are ever
   worth tracking — currently only the first is even recorded) if the
   iterative approach proves awkward for some case not yet identified.
2. **One shared resolution helper, used by every call shape above** —
   something like `SymbolDatabase::resolveMethod(className, methodName,
   curPath)`: look up `methodName` directly in `className`'s own scope
   first (`findSymbolsInScope`, cheap, matches today for the common case of
   a method declared on the class actually being called); on a miss, walk
   `baseClassChain` outward one class at a time, returning the first match.
   `Class::method(...)` resolution passes the named class directly;
   bare-in-class-body resolution passes `enclosingClassNameAt(path, line)`
   (already exists, currently only used by completion's `super`/`this`
   case); dotted-call resolution keeps its existing single-hop behavior but
   gains the same multi-level walk for the ordinary (non-`super`) case.
3. **Update each call site to use it:**
   - `enterTf_call`/`parseCallHeader` need to *retain* the scope prefix
     (`Class::`/`pkg::`) they currently discard, so the resolver has
     something to resolve against — a small extraction change, not just a
     resolver change.
   - §6.23's `checkMissingArguments`/`pickCallee`
     (`src/db/compilation_controller.cpp`) and signature help's bare-call
     path (`signature_help.cpp`) both switch from `findSymbolsByName` +
     same-file tie-break to the new shared resolver.
   - `completion.cpp`'s `resolveFirstSegment`/`resolveMemberSegment` gain
     the multi-hop walk for the non-`super` case (removing the "one hop
     only" limitation disclosed above).
   - Hover/definition: out of scope for the first pass (see below) but the
     new resolver should be written so plugging them in later is a small
     change, not a rewrite.
4. **Fail-closed semantics preserved throughout** — if `resolveMethod` can't
   find a match anywhere in the chain (unresolved import, genuinely
   ambiguous multiple-inheritance case for an interface class, or a name
   this project's own parser doesn't yet capture), every caller's existing
   "silently skip, not this check's job" behavior stays exactly as it is
   today. This section is about resolving *correctly* when resolution is
   possible, not about flagging cases where it isn't.

**Deliberately out of scope for a first pass:**
- Interface classes' additional (beyond-the-first) `extends` parents —
  already a disclosed, separate limitation; a real fix needs the schema
  table mentioned in step 1 above, not just the iterative `detail`-chain
  walk.
- Hover/go-to-definition switching over to the new resolver — listed above
  as a natural follow-on, not committed to here, to keep this section's own
  first pass scoped to the call shapes that actually produced the 349 real
  false positives.
- Package-qualified calls (`pkg::func(...)`) where `pkg` names a package,
  not a class — `qualifiedClassScope`'s existing package-nesting handling
  (§6.17) may already cover this correctly for dotted/member lookups; needs
  confirming against this section's new resolver, not assumed to already
  work.
- Re-running the full UVM-corpus probe from §6.23's own investigation is
  this section's own acceptance test — see "Unit tests" below.

**Unit tests, once implemented:** a `Class::method(...)` call resolving to
that exact class's own method even when a same-named, differently-shaped
method exists elsewhere in the DB (the core `type_id::get()`-style
regression case); a bare in-class call resolving to an inherited method
several `extends` levels up rather than an unrelated same-named method
elsewhere; dotted-call completion offering a method inherited two or more
levels up an ordinary (non-`super`) object's class hierarchy; the existing
`super.` one-hop case still passing unchanged; an unresolvable case (unknown
class, cyclic `extends` guard) still failing closed with no diagnostic/no
crash. **Acceptance test:** re-run the exact §6.23 UVM-corpus probe
(`tests/uvm_corpus/test_signature_help_uvm.cpp`'s `[.probe]` case, or its
replacement) and confirm `PROBE-TOTAL` drops to 0 (or every remaining hit is
a confirmed genuine bug in the corpus itself, not a resolution failure).

**Implementation notes (what actually landed, 2026-09-20):**
- `SymbolDatabase::baseClassChain`/`resolveMethod` (`src/db/symbol_database.h/.cpp`)
  built exactly per the design sketch's steps 1-2 above — no schema
  migration, the existing `symbols.detail` parent-name string walked
  iteratively with a cycle guard, and an internal same-file-preferred
  tie-break duplicated locally (matching `compilation_controller.cpp`'s own
  pre-existing `pickCallee` precedent, since `svlsp_db` has no dependency on
  `svlsp_lib`'s `lsp::pickBestSymbol`).
- `CallRecord` (`parse_record.h`) gained a `calleeScope` field;
  `enterTf_call` (`sv_tree_walker.cpp`) now retains the *immediate*
  `Class::`/`pkg::` segment (the one directly before the call name — e.g.
  "type_id", not "T", for the doubly-qualified `T::type_id::create(...)`
  idiom) instead of discarding it. `signature_help.cpp`'s `parseCallHeader`
  gained the equivalent lexical scan, returning a `CallHeader{name, scope}`
  instead of a bare name.
- **The open "package-qualified calls" question above was resolved, not left
  open: an explicitly-scoped call (`calleeScope`/`CallHeader::scope`
  non-empty) whose scope name does *not* resolve to a known Class —
  `baseClassChain` comes back empty, whether because it names a package, an
  unresolved typedef alias (UVM's own `typedef ... type_id;` idiom — the
  actual majority case in the corpus), or a genuine unknown — is now
  deliberately skipped entirely (fails closed), never falls back to a flat
  whole-database search.** This was the safer of the two choices available
  and is what actually eliminates the `type_id::get()`-shaped false
  positives (348 of the 349 corpus hits): falling back to a flat search for
  "scope named but unresolved" would have reintroduced exactly this
  section's own bug for that case. The cost is that a genuine
  `pkg::func(...)` call (a real package-scoped function) is no longer
  checked by §6.23/signature-help at all, where before it was attempted via
  flat search (and could coincidentally work or coincidentally collide) --
  an honest trade, not a regression in the direction that matters (a missed
  check, never a wrong diagnostic). Properly distinguishing "known package"
  from "known class" from "truly unknown" so packages could be resolved
  correctly instead of skipped is a further, separate follow-up, not
  attempted here.
- A **bare** (unqualified) call still falls back to the pre-existing flat
  `findSymbolsByName` + same-file tie-break, but only when there is no class
  context at all to have gotten wrong: no enclosing class (an ordinary
  module/program-scope call), or the enclosing class's own hierarchy simply
  doesn't declare that name (most likely a legitimate call to an
  outer-scope free function from inside a method body — legal SV, and
  falling back here is not the same risk as the explicitly-scoped case
  above, since there was never a specific class being named to get wrong).
- `completion.cpp`'s single-hop `qualifiedClassScope` helper became fully
  dead code once `candidatesForResolvedType`/`resolveMemberSegment` were
  switched to a chain-walking `membersAcrossChain` helper (built directly on
  `baseClassChain`) — deleted rather than left unused, per this project's
  own no-dead-code convention.
- Hover/go-to-definition were **not** touched, exactly as scoped out above.

---

### 6.27 Dotted-Call Signature Help (`obj.method(`, `obj.field.method(`)

**Status:** implemented 2026-09-20.

**Motivation:** signature help's disclosed scope limit since §6.22's follow-up
(and unchanged by §6.26) was that a *dotted* call (`obj.method(`) returned
null unconditionally — `parseCallHeader` fails closed the moment a `.`
precedes the call name, since resolving the receiver's type needs the same
chain-resolution machinery dot-completion has, which this file's own lexical
scan never attempted. A user prompt asked for tests exercising exactly this
shape (a package-scoped class-typed variable, a dot-chained call, real
`import` statements) — surfacing that the feature itself didn't exist yet to
test, not just a coverage gap. Implemented before adding those tests, rather
than writing tests against documented-null behavior.

**Design — reuse dot-completion's machinery rather than reimplement it:**
1. **Relocated, not rewritten:** `resolveChain`, `resolveFirstSegment`,
   `resolveMemberSegment`, `membersAcrossChain`, `peelDimensionLayers`
   (plus its own file-local `splitLayers`/`isContainerDimensionTag` helpers)
   moved from `completion.cpp`'s anonymous namespace to `lsp/symbol_utils.h/.cpp`
   — a header both `completion.cpp` and `signature_help.cpp` already
   include, and the natural home given `dotCompletionContext`/`ChainSegment`/
   `DotCompletion` already lived there. No behavior change, purely a visibility
   change so a second file can call them; `completion.cpp` itself is
   unaffected (same names, now free functions instead of anonymous-namespace
   ones). A new `positionForOffset(text, offset)` (`symbol_utils.h/.cpp`) —
   the inverse of every provider's own local `toOffset` — was added
   alongside them, needed to convert `findEnclosingParen`'s byte-offset
   coordinate system into the (line, character) pair `dotCompletionContext`
   expects.
2. **`parseDottedCallHeader`** (new, `signature_help.cpp`, file-local):
   tried only after `parseCallHeader` fails to find an unqualified or
   `::`-qualified name (i.e., exactly when a `.` precedes the call name).
   Calls `dotCompletionContext` with the cursor positioned at the call
   name's own end (not the user's actual cursor, which may be deep inside a
   multi-line argument list) — a small but load-bearing trick: this makes
   `dotCompletionContext` return exactly the *receiver* chain as `segments`
   (everything before the last `.`) and the call's own name as `prefix`,
   with zero new parsing logic. `resolveChain` then resolves the receiver
   chain to its declared type, and `SymbolDatabase::resolveMethod` (§6.26)
   finds `prefix` as a Function/Task on that type or one of its ancestors —
   the same inheritance-aware resolution the bare/`Class::`-qualified paths
   already use. Fails closed (null) if the receiver chain or the method
   itself doesn't resolve.
3. `signature_help.h`'s header comment rewritten to document three
   call-header shapes instead of two.

**A real, disclosed limitation found verifying this against the UVM corpus,
not assumed:** a dotted call from *inside an out-of-class method body*
(`function void uvm_component::set_domain(...); ... endfunction`, a real
corpus site: `m_children[c].set_domain(domain)`) still returns null — but
for a different, deeper reason than "dotted calls aren't supported," since
that's no longer true. Traced (not assumed) to an already-disclosed,
separate gap this section's own UVM-corpus test file already documented in
its header comment: an out-of-class method body's own recorded scope isn't
nested under its declaring class at all (the same category of gap "Dot-
completion into a package-nested class's members," §6.17, fixed for a
sibling case) — so `m_children` (a member of `uvm_component`) is never
visible to `findSymbolsVisibleAt` from inside `set_domain`'s own
out-of-class body, and the receiver chain fails before `resolveMethod` is
ever reached. A dotted call from inside an *in-class* method body (this
section's own new tests, and the UVM corpus's other two `do_execute_op`
cases) is unaffected. Not fixed here — flagged as a real, separate follow-up
(fixing it would also improve hover/completion/definition for the same
out-of-class-body member-visibility gap, not just signature help), scoping
this section to what dotted-call signature help itself needed.

**Unit tests:** `tests/unit/lsp/test_signature_help.cpp` — a basic dotted
call to the receiver's own declared method; a dotted call resolving to a
method inherited from an ancestor class; a two-segment chain
(`obj.field.method(`); the pre-existing "null for a dotted call" test
retitled and re-commented (the receiver is genuinely undeclared in that
fixture, which is why it's still null — not because dotted calls are
unsupported, which is no longer accurate). Three further cases driven
through a **real** `CompilationController::compile` pipeline rather than
hand-built `SymbolRow`s (`RealCompileFixture`, tagged `[real-compile]`),
specifically requested to prove the combination parses and resolves
end-to-end, not assumed from the import-free synthetic tests alone: a
specific import (`import pkg_a::ClassB;`) on its own line, a package-
qualified variable declaration (`pkg_a::ClassB a;`), and a dotted call
(`a.get_something(1, 2)`); the same with a wildcard import
(`import pkg_a::*;`); and the same again where the called method is
declared not on `ClassB` itself but on `BaseClass` in a *different*
package (`pkg_base`) that `pkg_a` itself imports, exercising
`baseClassChain`'s cross-package `extends` walk together with a real
multi-package compile. All three passed on the first real-corpus-shaped
run. Confirmed along the way (documented above, not assumed): the `import`
statements in all three are cosmetically present but not actually
load-bearing for this resolution — `baseClassChain`/`resolveMethod` resolve
a bare class name across the whole database regardless of import
visibility, same as the `qualifiedClassScope` logic they replaced in §6.26;
only the receiver variable's own visibility (via `findSymbolsVisibleAt`)
would ever depend on an import, and these tests declare `a` directly in the
same module, not via a wildcard-imported package-level variable.

UVM-corpus test updated: the third `signature help` case in
`tests/uvm_corpus/test_signature_help_uvm.cpp` (`m_children[c].set_domain(domain)`)
re-titled and re-commented per the disclosed limitation above — still
asserts null, now for the traced, accurate reason. Full unit suite: 2168
assertions/667 cases. Full UVM-corpus suite: 505 assertions/14 cases, no
regressions. Full Emacs functional suite: 207/207, no regressions. Commits:
`cb51a68` (feat + tests), `b21882c` (docs).

**Functional test added 2026-09-21** (closing the gap the entry above left:
unit and UVM-corpus coverage only, no real-client round trip) —
`tests/integration/test_12_signature_help.sh` gained three new cases against
a fixture addition (`rrsh_sh_*`, appended to the end of
`fixtures/ref_rename_sighelp.sv` so the three pre-existing tests sharing that
fixture, incl. `test_07_references.sh`/`test_11_rename.sh`, keep their
hardcoded line numbers valid), each covering a distinct dotted-call scoping
rule and asserting the *full* parameter list (label string, each
`parameters[]` entry's own label, and `activeParameter`), not just
non-null/count — the point being to actually exercise a real function's own
input/direction/default rendering over the wire, not just call resolution:
own-class method resolved via a specific `import rrsh_sh_pkg::rrsh_sh_child;`
(`rrsh_sh_obj.rrsh_sh_child_get(`); a method inherited through `extends`,
two parameters with the second defaulted, so default-value rendering is
covered on the dotted path too (`rrsh_sh_obj.rrsh_sh_base_get(`); and a
two-segment chain through a field declared with its full package-qualified
type and never imported at all (`rrsh_sh_holder_obj.rrsh_sh_field.
rrsh_sh_child_get(`) — a different scoping rule than the specific-import
case. All three passed first run. Full Emacs functional suite re-run:
210/210 (207 + 3 new), no regressions. Not yet committed — only commit when
asked.

---

### 6.28 Module/Interface/Program Port Type Information in Signature Help

**Status:** implemented 2026-09-22.

**Motivation:** function/task signature help (§6.22's follow-up) renders each
parameter as `"<direction> <type> <name>"` (e.g. `int width`, `output bit
valid`) — the type comes from `data_type_or_implicit()` captured in
`enterTf_port_item` (`sv_tree_walker.cpp`). Module/interface/program
instantiation signature help (the original §6.22 shape, `<TypeName>
<InstanceName> (`) never got the same treatment: `enterAnsi_port_declaration`
(`sv_tree_walker.cpp:508-522`) records only the port's direction as its
`detail` — `pushId(ParseRecordKind::Port, portId->IDENTIFIER(), ctx,
currentScope(), dir)` — never the port's own type, so `portLabel()`
(`signature_help.cpp`) falls through to its documented `"<direction>
<name>"` shape for every module port, regardless of whether that port is
typed `int`, `MyIfcClass`, or anything else. A user instantiating a module
with a wide/typed port list currently gets no type hint at all from
signature help — only `output my_port`, not `output logic [7:0] my_port`.

**Design sketch — mirror `enterTf_port_item`'s existing prefix/suffix
convention, don't reinvent it:**
1. `enterAnsi_port_declaration` needs the port's data type, not just its
   direction. The grammar (`grammar/Sv.g4:192-213`) exposes it three ways
   depending on which alternative of `ansi_port_declaration` matched:
   `net_port_header`'s own `net_port_type()`, `variable_port_header`'s own
   `variable_port_type()`, or (for the bare `interface_port_header`
   alternative) nothing to add — an interface port's "type" is the
   interface name itself, already implicit in the port's own declared
   shape, not a separate `data_type`. Each needs its own `m_tokens->getText(...)`
   call, same as `enterTf_port_item` line 480's `m_tokens->getText(dtoi)`.
2. Build `detail` the same way `enterTf_port_item` already does: type prefixed
   before direction only when direction is non-default/informative, or
   simplest — always `"<direction> <type>"` since module ports (unlike
   function parameters) don't have `enterTf_port_item`'s "input is the
   overwhelming majority, suppress it" noise concern to the same degree;
   worth checking real corpus output before deciding, not assumed up front.
   No default-value suffix needed here — `PARAM_DEFAULT_VALUE_SEP` already
   exists for `ansi_port_declaration`'s own `('=' constant_expression)?tail`,
   which today is silently dropped the same way type currently is; folding
   default-value rendering in here too (module ports can have them, e.g.
   `input int width = 8` on a module header) is in scope alongside type,
   not a separate follow-up, since both are the same "detail only carries
   direction" gap.
3. `portLabel()` (`signature_help.cpp`) needs no changes if `detail` is built
   to already match the `"<prefix>[ <PARAM_DEFAULT_VALUE_SEP> = <value>]"`
   convention `enterTf_port_item` established — confirm this by reading
   `portLabel()` again before implementing, not assumed.
4. Non-ANSI port declarations (`enterAnsi_port_declaration`'s sibling that
   doesn't exist yet — non-ANSI ports currently aren't recorded as `Port`
   rows at all, confirmed by grep: only `enterTf_port_item` and
   `enterAnsi_port_declaration` call `pushId(ParseRecordKind::Port, ...)`
   anywhere in `sv_tree_walker.cpp`) are a pre-existing, separate gap —
   out of scope here, not silently fixed as a side effect.

**Verification plan:** unit tests in `tests/unit/lsp/test_signature_help.cpp`
covering a module instantiation with typed ports (`logic`, `int`, a
user-defined type/class-like port, an untyped/implicit port) and a default
value on a module port; a real UVM-corpus check for label-formatting
surprises (unusually wide types, parameterized ports) before calling this
done, same discipline §6.27 used; functional-test coverage via
`tests/integration/test_12_signature_help.sh` for a real end-to-end
`initialize`/`didOpen`/`signatureHelp` round trip against a module
instantiation, not just the function-call cases it already has.

**Implementation, following the design sketch above almost exactly, one
decision resolved by checking real code rather than assuming:**
`enterAnsi_port_declaration` (`sv_tree_walker.cpp`) now extracts `type` from
whichever header matched — `nh->net_port_type()` or `vh->variable_port_type()`
via `m_tokens->getText(...)`, same verbatim-token-stream technique
`enterTf_port_item` already uses for the identical "preserve real inter-token
spacing" reason (`ctx->getText()` would collapse `"var int"` to `"varint"`).
`net_port_header`/`variable_port_header` are mutually exclusive alternatives
of the same grammar rule (`ansi_port_declaration`, `grammar/Sv.g4:208-213`),
so at most one of `nh`/`vh` is ever non-null — restructured as an `if`/`else
if` rather than the pre-existing code's `dir.empty()`-gated cascade, which
worked but obscured that mutual exclusivity. `interface_port_header` gets no
type extraction, exactly as the design sketch called for: its own header
text (`bus_if.master`, `interface.mp`, ...) already **is** the port's type,
so there's no separate `data_type` to pull out — confirmed against a real
interface-port instantiation in `examples/interfaces.sv`
(`bus_if.master bus`), not assumed; its `detail` stays empty, matching the
pre-existing (undirectioned) interface-port shape.

**The "always show direction, don't suppress default `input`" question the
design sketch left open was resolved by reading the pre-existing code, not
by guessing:** `enterAnsi_port_declaration` never had `enterTf_port_item`'s
"input is the overwhelming majority, suppress it" logic to begin with — a
module port's explicit `input` was always kept in `detail` before this
change (`"input"`, not `""`). Matching that existing convention (rather than
introducing suppression as a side effect of an unrelated type-info change)
keeps this section's diff to exactly the type/default-value gap it set out
to close.

**Default-value rendering folded in too, per the design sketch's own call:**
`ctx->constant_expression()` (present on 3 of `ansi_port_declaration`'s 4
grammar alternatives) is rendered the same way `enterTf_port_item` already
renders a function parameter's default — appended after `PARAM_DEFAULT_VALUE_SEP`,
split back apart by the same `portLabel()` (`signature_help.cpp`), confirmed
to need zero changes for this (its `"<prefix>[ <PARAM_DEFAULT_VALUE_SEP> =
<value>]"` split/join logic is already fully generic over which caller
populated `detail`).

**Verified against real code, not just synthetic fixtures, before calling
this done (the design sketch's own "same discipline §6.27 used" instruction
— the UVM corpus itself is a pure class library with no modules/interfaces
at all to exercise this against, per §6.22's own prior finding, so
`examples/module_params.sv` and `examples/interfaces.sv` served as the real
corpus instead):** `module_params.sv`'s packed-dimension port
(`input logic [WIDTH-1:0] d`) renders as `detail == "input logic [WIDTH-1:0]"`
— bracket spacing preserved correctly, no surprises from the verbatim-token
extraction on a multi-token, non-identifier-shaped type. `interfaces.sv`'s
`bus_if.master bus`/`bus_if.slave bus` ports both render `detail == ""`
exactly as designed. Confirmed via a throwaway scratch test case (added,
inspected, then deleted — never landed in the tree) rather than assumed from
the grammar reading alone.

**Unit tests:** `tests/unit/compiler/test_sv_listener.cpp` — the three
pre-existing "port direction in detail" cases updated in place (`"input"` →
`"input logic"`, etc., since the port `detail` shape genuinely changed, not
a broken assertion) plus 6 new `[phase6.28]` cases: a plain typed port
(`int`), a user-defined-type port, a `wire`-typed port (documenting that
`net_port_type` captures the net-type keyword itself, not truly "untyped"),
a default value past the separator, an interface port's empty detail, and a
`variable_port_header`/`var`-keyword port. `tests/unit/lsp/test_signature_help.cpp`
— 3 new `[phase6.28]` cases proving `portLabel()` needed no changes: a typed
module instantiation (`input int width`, `output bit valid`) rendering both
parameter labels correctly, a module port default value rendering after the
name, and a direction-only (untyped) port still falling back to its
pre-existing `"<direction> <name>"` shape. Functional test: a new case in
`tests/integration/test_12_signature_help.sh` (`rrsh_p28_*`, appended to the
end of `fixtures/ref_rename_sighelp.sv` so every pre-existing hardcoded line
number stays valid) — a real `initialize`/`didOpen`/`signatureHelp` round
trip against `rrsh_p28_wide(input int rrsh_p28_width, output bit
rrsh_p28_valid = 1)`, asserting the full signature label, each parameter's
own label, and `activeParameter`, not just non-null/count.

Full unit suite: 2196 assertions/676 cases (was 2168/667), no regressions.
Full UVM-corpus suite: 505 assertions/14 cases, unchanged — this section
touches no code path that corpus exercises (class-library-only, no
modules/interfaces), so an unchanged count is itself the expected, correct
result, not a "didn't run" false negative. Full Emacs functional suite:
211/211 (210 + 1 new), no regressions. Not yet committed — only commit when
asked.

---

### 6.29 Signature Help for Macros, SystemVerilog Keyword Constructs, and System Tasks/Functions

**Status:** not started.

**Motivation:** signature help today (§6.22, its follow-up, §6.26, §6.27,
§6.28) covers exactly the call shapes that resolve to user-declared `Port`
rows in `SymbolDatabase`: module/interface/program instantiations, bare and
`Class::`/`pkg::`-qualified function/task calls, and dotted method calls.
Three shapes users type constantly get nothing at all:
1. **Macro invocations** — `` `uvm_info(ID, MSG, VERBOSITY) ``,
   `` `uvm_error(ID, MSG) ``, user `` `define ``s with parameters.
2. **Keyword constructs with a parenthesized header** — `if (`, `while (`,
   `for (`, `foreach (`, `repeat (`, `case (`/`casez (`/`casex (`,
   `wait (`, `assert (`/`assume (`/`cover (`, `disable iff (`,
   `randomize(...) with`, `std::randomize(`.
3. **System tasks/functions** — `$display(`, `$sformatf(`, `$fopen(`,
   `$cast(`, `$clog2(`, `$urandom_range(`, `$fatal(`, ... — built into the
   language, never declared anywhere a DB row could come from.

**Confirmed by reading the code directly, not assumed:**
- `isIdentChar` (`src/lsp/signature_help.cpp:10-13`) already treats `$` as
  an identifier character, so `parseCallHeader` today *does* read
  `$display` as the callee name — it just finds no `Function`/`Task` row for
  it and returns null. System-task support therefore needs a lookup
  fallback, not new header-scanning.
- `` ` `` is **not** an identifier character there, so a macro call header
  is currently read as the bare name without its backtick (`uvm_info`) —
  which could in principle falsely match a same-named function. Macro
  support must scan for the backtick explicitly and route to a separate
  lookup before the function/task path.
- `MacroRecord` (`src/compiler/sv_preprocessor.h:7-11`) still has no
  parameter list, and `ParseRecordKind::Macro` is still never constructed
  (§6.25's findings, unchanged).
- `SignatureHelpOptions{}` (`src/lsp/server_state.cpp:27`) advertises no
  `triggerCharacters`/`retriggerCharacters`; clients fall back to their own
  defaults. Worth revisiting here since all three new shapes are `(`/`,`
  driven like the existing ones.
- Precedent for a static, LRM-derived table already exists:
  `src/lsp/sv_builtin_methods.h` (§6.13) and `src/lsp/sv_keywords.h` (§6.9),
  both with an explicit "reconstructed from training-time familiarity with
  IEEE 1800-2017, not verified against an LRM copy" disclosure.

**Design sketch:**

*A. Macros* — builds on §6.25; §6.25's investigation questions (1)-(4) are
the first step of this part, and its "investigated, not worth it" outcome
remains valid for this part alone without blocking B/C.
1. Retain the parameter list `parseMacroDefinition` already parses onto
   `MacroRecord` (names plus optional `` `define FOO(a, b=1) `` defaults).
2. Persist macros as `ParseRecordKind::Macro` rows (with parameters as child
   rows or encoded in `detail`) so signature help — and, as a side effect,
   hover/definition/workspace-symbol for macros — can find them; §6.25 (2)
   decides DB vs a preprocessor-local table, including `` `include ``d and
   redefined-macro semantics (last active definition wins).
3. Signature help: detect `` `name ( `` in the *original* document text
   (the cursor is in the unexpanded buffer the client holds, so no
   `sourceMap` translation is needed for the call site itself — only the
   definition's location), count top-level commas with the existing
   `activeParameter` scanner, and render `` `name(a, b = 1) ``.
4. Macros from files outside the open document (UVM's `uvm_macros.svh`)
   resolve only if that file was compiled into the project DB or a
   pre-built library DB (§6.19) — no special UVM table.

*B. Keyword constructs* — a small static table (`sv_keyword_signatures.h`
or an extension of `sv_keywords.h`) mapping each keyword to one or more
"signatures" describing its header shape, e.g.
`for (initialization; condition; step)`, `foreach (array[index, ...])`,
`case (expression)`, `assert (expression) [else ...]`,
`repeat (count)`, `wait (expression)`, `disable iff (expression)`.
1. Header detection: the identifier before `(` is a keyword from the table
   (checked *before* the function/task DB lookup, since keywords can never
   be user function names).
2. Separator-aware `activeParameter`: `for` uses top-level `;`, not `,`;
   `foreach` highlights the loop-variable segment inside `[...]`; the
   existing comma counter needs a per-signature separator parameter.
3. Open question to settle before implementing: which keywords actually
   earn signature help vs being noise (`if (` / `while (` have a single
   obvious parameter — possibly only worth doing for `for`, `foreach`,
   `case*`, `randomize ... with`, and the assertion forms). Decide from real
   editor use, not up front.

*C. System tasks/functions* — a static table `src/lsp/sv_system_tasks.h`
in the same shape as `sv_builtin_methods.h`: name, parameter list (with
optional/variadic markers), short documentation string. Initial coverage,
grouped by IEEE 1800-2017 Clauses 20/21:
- display/format: `$display`, `$write`, `$strobe`, `$monitor`, their
  `b`/`h`/`o` variants, `$sformatf`, `$sformat`, `$swrite`, `$fdisplay`,
  `$fwrite`
- file I/O: `$fopen`, `$fclose`, `$fgets`, `$fgetc`, `$fscanf`, `$sscanf`,
  `$feof`, `$readmemh`, `$readmemb`, `$writememh`, `$writememb`
- simulation control / severity: `$finish`, `$stop`, `$exit`, `$fatal`,
  `$error`, `$warning`, `$info`
- time: `$time`, `$stime`, `$realtime`, `$timeformat`, `$printtimescale`
- conversion / casting: `$cast`, `$signed`, `$unsigned`, `$itor`, `$rtoi`,
  `$bitstoreal`, `$realtobits`, `$typename`
- math: `$clog2`, `$ln`, `$log10`, `$exp`, `$sqrt`, `$pow`, `$floor`,
  `$ceil`, trig functions
- bit-vector / array query: `$bits`, `$countones`, `$countbits`, `$onehot`,
  `$onehot0`, `$isunknown`, `$size`, `$dimensions`, `$left`, `$right`,
  `$low`, `$high`, `$increment`
- random: `$urandom`, `$urandom_range`, `$random`, `$dist_*`
- plusargs / assertion control: `$test$plusargs`, `$value$plusargs`,
  `$asserton`, `$assertoff`, `$assertkill`
1. Lookup: after the DB function/task lookup fails for a `$`-prefixed
   name, consult the table. Unknown `$names` (vendor/PLI tasks) stay null.
2. Variadic handling: `$display(fmt, args...)` — `activeParameter` clamps
   to the variadic tail once past the fixed parameters; `$fatal`'s leading
   optional `finish_number` rendered as optional.
3. Optionally reuse the same table for `$`-triggered completion (not
   offered today) — a small follow-up, not required for this section.

**Shared infrastructure:** all three parts add a lookup path to
`SignatureHelpProvider::getSignatureHelp` ahead of / after the existing
three shapes; order: macro (`` ` `` prefix) → keyword → instantiation →
bare call (DB, then system-task table for `$` names) → dotted call. The
header comment in `signature_help.h` must be updated to list the new
shapes. Consider advertising `triggerCharacters = {"(", ","}` in
`server_state.cpp` once all shapes land.

**Verification plan:** unit tests in `tests/unit/lsp/test_signature_help.cpp`
per part (`[phase6.29]`): macro with/without defaults, macro vs same-named
function disambiguation, nested call inside a macro argument; `for`
`;`-separated active index, `foreach` bracket segment, `case (`; `$display`
variadic clamping, `$fatal` optional leading argument, unknown `$vendor_task`
returning null. Preprocessor unit tests for the widened `MacroRecord`.
UVM-corpus check that `` `uvm_info ``/`` `uvm_error `` resolve when
`uvm_macros.svh` is compiled in. Functional coverage via
`tests/integration/test_12_signature_help.sh` (append fixtures to the end of
`fixtures/ref_rename_sighelp.sv` so existing line numbers stay valid), one
real round trip per part.

**Out of scope:** hover/completion for system tasks and keywords (possible
follow-ups sharing the new tables); signature help for built-in container
methods (`q.push_back(`, `mbx.put(` — `sv_builtin_methods.h` has only a
one-line `detail`, not structured parameters; a natural later extension of
part C's table format); parameter-override blocks (`Module #(`).

---

### 6.30 Scope-Aware Symbol Resolution for Hover, Definition, References and Rename

**Status:** step 1 (same-file wildcard import) implemented 2026-09-24; steps
A–D not started. Planned 2026-09-24. The failing cases are already
committed as `[!shouldfail]` tests (`ad85a28`).

**Motivation:** `HoverProvider`/`DefinitionProvider` resolve the bare word
under the cursor with `findSymbolsByName` + `pickBestSymbol` (same file
first, then declaration-like kind, then `path, line` order), and
`ReferencesProvider`/`RenameProvider` do a lexical whole-word search gated
only on the name existing *somewhere*. None of them looks at `::`, `.`,
imports, or the lexical scope. They are correct only when a name is
unique. §6.26/§6.27 already made signature help and completion
scope/type-aware. These four features are what's left, and they're the ones
users hit most.

The gap is pinned down by committed tests, each with a same-named decoy
a name-only lookup would pick and an exact file/line/column assertion:
- `tests/unit/lsp/test_definition_scoped.cpp`: 23 cases, 17
  `[!shouldfail]`:
  - `::`: `pkg_b::Item`, `pkg_b::make()`, `pkg_b::Item::stat()`,
    `Class::method()`, and `p::Shadowed` hijacked by a same-named class in the
    primary file
  - imports: `import pkg_b::*`, `import pkg_b::Item`, a bare call via a
    wildcard import
  - `.`: `obj.get()`, `obj.val`, an inherited method, `a.inner.get()`,
    `this.field`, `super.method()`
  - lexical: a signal in another module, a local variable shadowing a
    class field, an argument shadowing a module signal
- `tests/unit/lsp/test_references_scoped.cpp`: 27 cases, 9
  `[!shouldfail]`:
  - 4 scope cases (same-named signal/local/field/method elsewhere wrongly
    included)
  - 5 names never recorded as symbols (typedef, enum literal, struct
    member, genvar, `` `define ``), so references returns null
- `tests/integration/test_40_scoped_definition_references.sh`: real-client
  round trips for the cases that already pass. The fix must keep these
  green.

**Confirmed by reading the code / a probe DB, not assumed:**
- Every symbol row already carries its fully-qualified scope, and function/
  task locals and arguments are scoped to the function (`m2::f` for `int a`
  and `int t` in `function int f(int a); int t;`). Scope rows carry
  `end_line`. So lexical resolution needs no new data:
  `findSymbolsVisibleAt(path, line)` already returns the local scope chain
  innermost-first.
- `begin … end` blocks and generate blocks are **not** scopes (a variable in
  `initial begin int blk; end` is scoped to the module). Two same-named
  variables in sibling blocks of one module stay indistinguishable. This is a
  disclosed limit here, not fixed.
- Scope containment is **line-granular** (`scopeAtPosition(path, line)`).
  Two scopes opening on one physical line can't be told apart. The fixtures
  put each scope on its own line. Also disclosed, not fixed.
- **A second, independent gap:** `findSymbolsVisibleAt` never sees a
  wildcard-imported package declared *in the same file*. Part 1 (local
  scope chain) only covers the cursor's own chain; Part 2 (cross-file
  top-level + wildcard packages) filters `f.path != ?`; only Part 3
  (specific imports) has no path filter. So `package p; … endpackage
  import p::*; module top; X x;` can't see `p::X`. This affects completion
  today as well. The definition tests put the packages in a separate file
  and so didn't catch it. Add a failing test first, then fix: drop the path
  filter for the wildcard-package scopes only, keeping it for `""` so the
  file's own top-level rows aren't duplicated by Part 1.

  **Implemented 2026-09-24 (step 1).** Part 2's condition is now
  `(f.path != ? AND s.scope = '') OR (s.scope IN (<wildcard pkgs>) AND NOT
  (f.path = ? AND s.scope IN (<local chain>)))`. The wildcard arm has no
  path filter; the `NOT (...)` guard stops a package that is also on the
  cursor's own scope chain from returning its rows twice (cursor inside the
  imported package in its own file). The wildcard arm is omitted entirely
  when there are no wildcard imports. Part 3 (specific imports) already had
  no path filter and is unchanged. Tests, each confirmed failing before the
  fix:
  - `tests/unit/db/test_symbol_database.cpp`: the same-file package is
    visible exactly once; a same-file non-imported package stays excluded.
    A third case guards against duplicates when the cursor is inside the
    imported package; it passed before the fix too and is kept as a
    regression guard.
  - `tests/unit/db/test_compilation_controller.cpp`: the same case through
    a real compile.
  - `tests/integration/test_17_import_resolution.sh` + new fixture
    `fixtures/import_same_file.sv` (`sfwi_` prefix): completion includes
    the imported package's class and excludes the other package's class.
    The positive case was confirmed failing against a binary built without
    the fix.

  None of the `[!shouldfail]` scoped tests flip here (they keep their
  packages in a separate file). Unit suite: 730 cases, 704 pass + 26 known
  gaps. Emacs suite: 224/224.
- Out-of-class method bodies are recorded with the enclosing
  *package's* scope, not the class's. `function void Item::ext();` inside
  `pkg_b` gives Function `ext` scoped `pkg_b`, and its locals `pkg_b::ext`,
  so the class's members are invisible inside the body (already disclosed in
  §6.27). This is pervasive in UVM.
- Signature help already resolves `.` chains
  (`dotCompletionContext` → `resolveChain` → `SymbolDatabase::resolveMethod`,
  `parseDottedCallHeader`), and `Class::`/`pkg::` calls (`parseCallHeader`'s
  scope scan + `resolveMethod`). `membersAcrossChain` gives fields and
  methods across the `extends` chain.
- Named connections look like `.` but aren't member access:
  `.port(sig)` / `.P(8)` inside an instantiation's `( … )` or `#( … )`.
  `signature_help.cpp` already tells them apart (`isNamedConnectionParen`,
  `parseInstantiationHeader`). The committed references tests for ports and
  parameters (lexical, passing today) cover exactly this shape.

**Design sketch:**

*A. One shared resolver.* New `src/lsp/symbol_resolution.h/.cpp` (in
`svlsp_lib`):
`resolveSymbolAt(db, path, text, position) -> std::optional<Resolved>`,
with `Resolved { SymbolRow row; bool exact; }`. `exact=false` marks a
name-only fallback (below). Steps, on the identifier under the cursor:
1. **`::`-qualified** (`A::B::name`): scan the qualifier segments leftward.
   Resolve the first segment as a Package (by name), a visible Class (via
   step 4's lookup), or `$unit` (scope `""`). Then walk each later segment
   inside the previous scope. The final lookup is `findSymbolsInScope`,
   or `membersAcrossChain` when the scope is a class (inherited static
   members).
2. **`.` member access:** first rule out a named connection. If the `.`
   is preceded (ignoring whitespace) by `(` or `,` inside an instantiation's
   port or `#(` parameter list, resolve to the instantiated
   module/interface/program's Port or Parameter of that name
   (reuse/move `isNamedConnectionParen`/`parseInstantiationHeader` from
   `signature_help.cpp`). Otherwise call `dotCompletionContext` at the
   word's end (§6.27's trick) → `resolveChain` → `membersAcrossChain` on the
   receiver type → the field or method named `word`. `this`/`super` already
   work through `resolveFirstSegment`.
3. **Declaration under the cursor:** falls out of step 4. The declaring
   row is visible from its own line.
4. **Bare name:** walk `findSymbolsVisibleAt(path, line1)` innermost-first
   and take the first row whose name matches. Two extra rules:
   - when inside a class (`enclosingClassNameAt`), inherited members
     (`membersAcrossChain`) outrank everything outside the class chain;
   - an explicit specific import outranks a wildcard import (LRM 26.3).
5. **Fallback, `exact=false`:** if a `::`/`.` qualifier or receiver
   can't be understood at all, fall back to today's
   `findSymbolsByName` + `pickBestSymbol`. Cases: an unknown package or
   class, or a receiver whose type isn't a class — a struct, interface or
   virtual interface, a hierarchical instance path like `u_dut.sig`, or a
   Port-typed receiver. Same for a bare name nothing visible declares
   (e.g. a UVM class used without `import uvm_pkg::*`). This is a
   deliberate departure from §6.26's "fail closed": hover/definition on
   those shapes work by luck today, and dropping them would be a
   regression. But when the qualifier or receiver **is** understood and the
   name isn't found there, return null rather than guessing (§6.26's own
   rule, same reason).

*B. Providers.*
- **Hover and definition:** call `resolveSymbolAt`, render `row`.
  `pickBestSymbol` stays as the fallback's tie-break only.
- **References:** resolve the cursor to `target`. If it's null, return
  null as today. Otherwise keep today's lexical candidate scan
  (`findIdentifierOccurrences` over `allFilePaths()`), then resolve each
  candidate occurrence and keep it if:
  - it resolves `exact` to the same row (compare by `id`), or
  - it resolves only by fallback (`exact=false`) and `target` is among
    that name's candidates.

  The second rule keeps uses we can't type-resolve (struct fields,
  hierarchical paths) instead of silently dropping real references.
  Over-inclusion there matches today's behavior; no new error. Declaration
  exclusion then uses `target` itself, not "any row with this name at this
  position".
- **Rename:** use exactly the filtered occurrence set from references
  (today `rename.cpp` has its own copy of the lexical scan). Rename is where
  a name-only match does real damage, since it edits the unrelated symbol too.

*C. Record the missing kinds* (flips the 4 non-macro "unrecorded"
references tests; independent of A/B and can land separately). New
`ParseRecordKind`s, with no schema change since `kind` is a string column:
- `Typedef` (plain `typedef`). A forward `typedef class Foo;` must not
  become a definition target that outranks the real class.
- `EnumLiteral`, scoped like the enum's own declaration.
- `Member` for struct/union fields, scoped
  `<enclosing>::<typedef-or-variable name>`.
- `Genvar`.

Each needs a mapping in `symbolKindFor`/`completionKindFor` and a check of
what documentSymbol/workspace-symbol should show. Dot-resolution into a
struct type stays out of scope; the `s.refs_hi` use is kept by B's
fallback rule. Macros are §6.25/§6.29 part A, so the macro test stays
`[!shouldfail]` until that lands.

*D. Out-of-class method bodies* (separable, but the UVM-corpus check
below depends on it). In `sv_tree_walker.cpp`, a
`function/task C::m(...)` body should push scope `…::C::m` (and resolve
`C` to its qualified class scope), so the body's locals and arguments
nest under the class and the class's members become visible. Then decide
whether the body's own Function row is recorded under the class, and how
it de-duplicates against the `extern` prototype row. `resolveMethod`
already picks the first of duplicates by same-file/earliest line (the
§6.22 corpus test relies on the prototype winning, so re-check that test).
This also closes the known §6.27 dotted-call gap
(`m_children[c].set_domain(...)`).

**Ordering:** wildcard-import same-file fix (with its own test, done) → A → B
(hover/definition first, then references + rename) → C → D. After each
step, remove the `[!shouldfail]` tag from every test that now passes:
Catch2 reports a `[!shouldfail]` test that passes as a failure, so each flip
is forced, not optional.

**Performance:** references now resolves every lexical hit instead of just
listing it. That's one visible-set query and possibly a chain resolution
per occurrence. For a common name across the UVM corpus (`get`, `name`)
that's thousands of resolutions. Cache per request:
- the visible set per `(path, line1)`
- the scope chain per `(path, line1)`
- resolved receiver types per `(path, line1, chain text)`

Measure on the UVM corpus (release build) before and after, and record
the numbers here. If it's still too slow, resolve only occurrences whose
file contains a declaration or import that could reach `target`.

**Verification plan:**
- every `[!shouldfail]` in the two scoped test files flips (except the
  macro case), tags removed;
- new unit cases: the same-file wildcard import; specific-over-wildcard
  import precedence; an inherited field used bare inside a derived class; a
  named port / `#(.P())` connection resolving to the module's Port /
  Parameter; the fallback path (struct field, hierarchical `u_dut.sig`)
  still returning today's answer; `this.`/`super.` inside an out-of-class
  body (D);
- existing hover/definition/references/rename unit tests unchanged;
- `test_40` gains the `pkg_b` decoy cases (appended to the end of
  `fixtures/defref_top.sv` so existing positions stay valid) plus a rename
  round trip;
- UVM corpus: definition/hover on a method called from an out-of-class
  body, and references on a commonly-overloaded method name (e.g. a class's
  own `get`), checked against a hand-verified count, not just non-null.
  Record the timing.

**Out of scope:**
- block-level (`begin`/`end`, generate block) scopes;
- column-granular scope containment;
- hierarchical references (`top.u0.sig`) and interface/virtual-interface
  member resolution — these use the fallback;
- `bind`;
- references into attached library DBs beyond what `allFilePaths()`
  already covers;
- macros (§6.25/§6.29).

---

## Appendix A — Technology Stack Summary

| Concern | Choice | Rationale |
|---|---|---|
| Language | C++20 | Performance; ecosystem for ANTLR4 and SQLite |
| Build system | CMake ≥ 3.20 | Industry standard for C++ |
| Unit test framework | Catch2 v3 | Header-friendly, good output, widely used |
| LSP framework | lsp-framework (pending evaluation) | Header-only, modern C++ |
| Parser generator | ANTLR4 (C++ runtime) | Mature, well-supported, grammar available |
| Database | SQLite3 (amalgamation) | Zero-dependency, sufficient for single-server use |
| Documentation | Markdown + Pandoc | Plain text, version-control friendly |
| Functional test client | Emacs daemon + lsp-mode | Real client; scriptable via emacsclient |

---

## Appendix B — Git Workflow

```
for each function/feature:
  1. Write unit test(s) — red
  2. Implement function — green
  3. Write functional/Emacs test (for all LSP features)
  4. git commit: "feat(<module>): <what the function does and why>"
  5. Update docs/ to reflect new capability
  6. git commit: "docs(<module>): document <feature>, test procedure, usage"
```

Branch strategy:
- `main` — always buildable and all tests (unit + functional) green.
- `phase/<N>-<short-name>` — work branches per phase.
- Merge to `main` only when unit tests and functional tests pass.

---

## Appendix C — Open Questions (to resolve in Phase 2)

1. Is there a C++ LSP framework more suitable than lsp-framework?
2. Is Catch2 preferred over GoogleTest for this project?
3. Should the grammar be taken verbatim or patched for SystemVerilog 2017/2023 compliance?
4. Should the server support TCP transport in addition to stdio (useful for remote development)?
5. ~~Should a `compile_commands.json`-style project file be used, or a custom project manifest?~~
   **Resolved (2026-07-09):** both — a custom `.svlsp.json` manifest and a VCS/Questa/
   Xcelium-style `.f` filelist, both producing one shared `ProjectConfig`. See §6.2 above
   and `/home/martin/.claude/plans/fluffy-hatching-popcorn.md`.
