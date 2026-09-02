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

### 3.10 Signature Help (`textDocument/signatureHelp`)
- Show port/parameter signatures when editing a module instantiation.
- Functional test: type a module instantiation; verify signature popup in Emacs.

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

**Why this is needed:** `CompilationController` is single-file: it recompiles a file
when its own content hash changes, but does not detect that other files referencing its
symbols may now be broken. If `MyClass` in `a.sv` loses a field and `b.sv` uses that
field, editing `a.sv` must eventually flag `b.sv` as stale and re-check it.

**What is missing:**
- A `file_dependencies` table `(dependent_file_id → files, dependency_file_id → files)`
  recording "dependent uses at least one symbol defined in dependency".
  - Populated during compilation: for each identifier reference resolved to a symbol
    in another file, record the edge. (Requires a reference-resolution pass, which is
    part of Phase 6 provider wiring anyway.)
- `CompilationController::compile(path, text)` after updating file A must:
  1. Query `file_dependencies` for all files that depend on A.
  2. For each dependent file, call `compile` recursively (or queue it for background
     recompilation) so its diagnostics are refreshed.
- Cycle guard: a file may indirectly depend on itself (mutual use); use a visited set.

**Integration tests (once provider wiring and import resolution are done):**
- `test_17_cross_file_error_propagation.sh`:
  1. Compile a project with `class_def.sv` (defines `MyClass` with field `x`) and
     `consumer.sv` (uses `MyClass.x`). Both show no diagnostics.
  2. Edit `class_def.sv` to remove field `x` via `didChange`.
  3. Verify that `consumer.sv` now has a diagnostic on the broken reference,
     without the user explicitly touching `consumer.sv`.
- `test_18_cross_file_error_cleared.sh`: restore field `x`, verify consumer clears.

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
  a simpler independent win.
- **Open question, needs investigation — pre-built/shared DBs for rarely-changing
  library code (UVM, verification IP):** a large fraction of a real verification
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

**Status:** not started.

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

**Status:** not started. Extends §6.10 (dot/member-access completion) and,
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
