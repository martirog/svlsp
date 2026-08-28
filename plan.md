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

### 6.8 Debounced / Asynchronous Compilation on `didChange`

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
