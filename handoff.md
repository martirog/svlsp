# svlsp — Handoff Document

**Last updated:** 2026-09-01 (compressed from full session history — see git log for
narrative detail if ever needed; this file now documents current-state-and-next-steps
only).

**Status:** Phases 3, 4, 5, 6.1, 6.2, 6.3 complete, plus §6.8 (debounced `didChange`
compilation) implemented 2026-08-28, plus a `pickBestSymbol()` hover/definition
disambiguation improvement (with unit + functional test coverage) implemented
2026-09-01 (see "Not yet done" #7 below), plus broadened UVM-corpus completion
coverage implemented 2026-09-01 (see "Not yet done" #8 below). Working tree clean,
all work through commit `253c8c9` is committed. Full unit suite: 1056 assertions /
405 test cases, all green (debug). Full Emacs integration suite:
146 passed / 0 failed (as of 2026-08-28; not re-run for the 2026-09-01 change).

---

## What this project is

A SystemVerilog Language Server Protocol (LSP) server written in C++20.
Full phased plan: `plan.md` (read this first for anything not covered below).

- **LSP protocol layer** (C++, lsp-framework) — talks to Emacs/editors
- **ANTLR4 compiler front-end** — parses SystemVerilog into an AST
- **SQLite database** — stores compilation artefacts for incremental builds and LSP queries
- **Emacs daemon + lsp-mode** — automated end-to-end functional test client

---

## Repository layout

```
src/lsp/           server_state, server, document_store, diagnostics,
                   hover, definition, references, completion, fuzzy_match,
                   document_symbols, workspace_symbols, rename,
                   signature_help, symbol_utils, project_manifest_parser,
                   project_registry — LSP layer
src/compiler/      compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                   parse_record, parse_cache, filelist_parser, project_config,
                   file_utils — compiler front-end
src/db/            database, symbol_database, compilation_controller,
                   library_resolver, project_compiler, schema — SQLite persistence (schema v5)
src/main.cpp       entry point (supports `--log-files <path>`, see below)
tests/unit/        Catch2 unit tests (391 cases, 963 assertions)
tests/integration/ Emacs functional test scripts (146 test cases across ~25 files)
tests/uvm_corpus/  opt-in test suite against a real, external UVM corpus (NOT in
                   ctest/make test — see "UVM corpus testing" below)
tools/             emacs-test-daemon.sh, emacs-test-init.el, emacs-test-lib.sh
examples/          .sv fixture files
grammar/           Sv.g4 — ~3830-line SystemVerilog grammar
cmake/             CMake helper modules (ANTLR4Tool.cmake: PATH → antlr4 cmd; fallback → download JAR)
build.debug|release/generated/antlr4/  generated SvLexer/SvParser/SvVisitor sources (not committed)
docs/              per-phase docs and architecture decision records
plan.md            full phased plan — read this first
```

---

## Build and test

```bash
# Build
cmake --preset debug && cmake --build --preset debug
# or: make configure build

# Unit tests
build/debug/unit_tests
build/debug/unit_tests "[compiler][parser]"   # subset by tag

# Integration tests (Emacs daemon, requires display or Xvfb)
DISPLAY=:99 make test-integration
bash tools/emacs-test-daemon.sh tests/integration/test_05_hover.sh   # individually

# Opt-in UVM corpus test suite (NOT part of ctest — see below)
cmake --build --preset release --target uvm_corpus_tests
./build/release/uvm_corpus_tests

# Smoke-test the binary directly
printf 'Content-Length: 152\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}' | build/debug/svlsp

# Log every file parsed/persisted (primary + every discovered include) to a file
build/release/svlsp --log-files /path/to/log.txt
```

Compiler: **g++-13** (system g++ 7.5 does not support C++20 — pinned in `CMakePresets.json`).
ASan + UBSan enabled in debug builds; use `release` for anything performance-sensitive
(debug/ASan can be 10-100x slower on large real-world input).

---

## Architecture of what exists

### State machine (`src/lsp/server_state.h/.cpp`)

```
Uninitialized ──(initialize)──► Active ──(shutdown)──► Shutdown ──(exit)──► Inactive
```
Wrong-state requests throw `lsp::RequestError`. `ServerState` also captures
`rootUri` and `explicitProjectConfigPath` (from `initializationOptions.svlsp.projectConfig`)
during `handleInitialize`.

Advertised capabilities: positionEncoding=UTF16, textDocumentSync=Full,
completion/hover/signatureHelp/definition/references/documentSymbol/workspaceSymbol/
rename all registered. **Note:** C++ designated initializers must follow
`ServerCapabilities` declaration order; `signatureHelpProvider` sits between
`hoverProvider` and `definitionProvider`.

### Document store / diagnostics / symbol_utils (`src/lsp/`)

- `DocumentStore`: pure `uri → {text, version}` map (`open`/`update`/`close`/`contains`/`get`).
- `DiagnosticsPublisher::publish(uri, version, diags)`: sends `publishDiagnostics`.
  **Known gap:** only ever called with the *primary* opened file's diagnostics
  (`compile()` returns `errsByFile[""]`) — diagnostics for transitively-included files
  are computed and persisted to the DB but never `publish()`'d to the client. A user
  opening a thin wrapper file sees "0 problems" even if included files have errors.
  Not fixed — see "Not yet done" below.
- `symbol_utils.h/.cpp`: `symbolKindFor`/`completionKindFor` (DB kind string → LSP enum),
  `wordAtPosition(text, line, char)` (extracts identifier at cursor, walks left even off
  an id char — intentional, completion needs the prefix), `makeRange`, `pathToUri`
  (`lsp::FileUri::fromPath` — **always absolutizes**; for a bare `` `include ``-resolved
  relative path that was never absolutized, use `lsp::Uri::parse("file:" + relPath)`
  instead, which preserves the literal path — this distinction matters for any code/test
  constructing a URI for a non-`didOpen`ed file).

### LSP feature providers (`src/lsp/<name>.h/.cpp`)

| File | Class | Behaviour |
|---|---|---|
| `hover.h/.cpp` | `HoverProvider` | `wordAtPosition` → `findSymbolsByName` → Markdown `**Kind** \`name\`` |
| `definition.h/.cpp` | `DefinitionProvider` | `wordAtPosition` → `findSymbolsByName` → `Location` |
| `completion.h/.cpp` | `CompletionProvider` | `findSymbolsVisibleAt(path, line1)` → fuzzy-scored (see below) → `CompletionItem[]` |
| `document_symbols.h/.cpp` | `DocumentSymbolsProvider` | `symbolsForFile` → `DocumentSymbol[]` with scope ranges |
| `workspace_symbols.h/.cpp` | `WorkspaceSymbolsProvider` | `findSymbolsByNamePrefix(query)` → `WorkspaceSymbol[]` |
| `references.h/.cpp` | `ReferencesProvider` | Returns `nullptr` — **unimplemented stub** |
| `rename.h/.cpp` | `RenameProvider` | Returns `nullptr` — **unimplemented stub** |
| `signature_help.h/.cpp` | `SignatureHelpProvider` | Returns `nullptr` — **unimplemented stub** |

Position-based providers (hover, definition, completion) check `m_store.contains(uri)`
first and return `nullptr` if the document isn't open. Hover/Definition do **global**
cross-file `findSymbolsByName` lookup via the shared `pickBestSymbol()` helper
(`src/lsp/symbol_utils.h/.cpp`): same-file match preferred, else a declaration-like
kind (`Module`/`Interface`/`Program`/`Package`/`Class`/`Function`/`Task`) preferred
over a data-like one (`Signal`/`Port`/`Parameter`/`Macro`), else the first row by
`ORDER BY path, line` — see "Known gaps" below for the residual alphabetical-tiebreak
risk this still leaves *within* a kind tier.

**Fuzzy completion matching** (`src/lsp/fuzzy_match.h/.cpp`): `fuzzyScore(candidate,
pattern) -> optional<int>`, case-insensitive subsequence matcher (not a subsequence →
`nullopt`; reordering never matches, only skipping is tolerated). Rewards contiguous
runs (+15), word-boundary landings (start of string, after `_`/`-`/`.`/`:`, or a
camelCase transition, +12), exact-case (+3); penalizes skipped chars (−1 each); shorter
candidate is the final tiebreak. `CompletionProvider` scores every visible row when a
prefix is typed, drops `nullopt` rows, sorts by descending score (name as stable
tiebreak), and assigns each item a zero-padded `sortText` for clients that re-sort by
that field. No typed prefix → unranked, unchanged behavior (every visible symbol).

### I/O wrapper (`src/lsp/server.h/.cpp`)

`LanguageServer` owns (construction order): `m_db`, `m_symbolDb`, `m_compiler`,
`m_projects` (`ProjectRegistry`), `m_connection`, `m_messageHandler`, `m_store`,
`m_diagnostics`, `m_dataMutex`, `m_debouncer` (`ChangeDebouncer` — declared/
constructed last so it's destroyed, and its worker thread joined, *first*, before
any member it calls back into). `m_db` is `":memory:"` — symbols lost on server
restart, repopulated per `didOpen`/`didChange` via
`m_compiler.compile(path, text, m_projects.configFor(path))`. Optional constructor
`std::ostream* logStream` (default `nullptr`, forwarded to `m_compiler`) backs
`--log-files`.

`registerHandlers()` wires: `initialize`/`initialized`/`shutdown`/`exit`,
`textDocument/{didOpen,didChange,didClose,hover,definition,references,completion,
documentSymbol,rename,signatureHelp}`, `workspace/symbol`.

**Debounced `didChange` (plan.md §6.8 — implemented 2026-08-28):** `didChange`
updates `DocumentStore` immediately, then calls `m_debouncer.schedule(uri.toString())`
instead of compiling inline — the actual compile+publish happens later, on
`ChangeDebouncer`'s own background thread, once no further edit for that URI
arrives within 300ms (`src/lsp/change_debouncer.h/.cpp` — a small standalone,
unit-tested worker: one thread, a `{key → deadline}` map, coalesces rapid
`schedule()` calls per key into a single fire, `cancel()` removes a pending one).
`didClose` calls `m_debouncer.cancel(uri.toString())` before closing. `didOpen`
still compiles synchronously (unchanged) since it only fires once per file, not a
burst source. New private `compileAndPublish(uri)` (used by both `didOpen` and
the debounce-fire callback) re-checks `m_store.contains(uri)` before compiling —
a harmless no-op if the document was closed in the narrow window between
scheduling and firing.

Since compiles can now run on a background thread concurrently with hover/
definition/completion/documentSymbol/workspace-symbol on the main thread, a
coarse `std::mutex m_dataMutex` guards every access to `m_store`/`m_db`/
`m_symbolDb`/`m_compiler`/`m_projects` — held across each full read or write in
every handler. This is the "minimum fix" plan.md §6.8 called for; not split into
finer-grained locks since none of these operations are hot enough to need it.

Real-world proof + regression coverage: `tests/unit/lsp/test_server_debounce.cpp`
drives a real `LanguageServer` over a real `Content-Length`-framed pipe
transport (`PipeStream : lsp::io::Stream`, an OS pipe pair) and asserts actual
diagnostic *content* changes correctly across a debounced edit (error → fixed →
error again) and that a rapid-fire burst of `didChange` collapses into exactly
one publish reflecting the final version — not just a timing/count check.

---

## Database layer (`src/db/`)

### Schema v5 (`src/db/schema.h`, `db::SCHEMA_VERSION = 5`)

- `files (id, path UNIQUE, content_hash, parsed_at)`
- `symbols (id, file_id, kind, name, line, col, parent, detail, end_line, scope)` —
  indexed on name, file_id, scope, (scope,name), (file_id,line,end_line)
- `diagnostics (id, file_id, line, col, message)`
- `imports (id, file_id, pkg_name, item, is_export)` — `item="*"` = wildcard,
  `is_export=1` = `export pkg::item`/`export pkg::*`
- `instantiations (id, file_id, type_name, inst_name, line)` — one row per
  module/interface/program instantiation; drives library resolution

Migrations (`database.cpp`, run automatically): v1→v2 adds end_line/scope; v2→v3 adds
`imports`; v3→v4 adds `imports.is_export`; v4→v5 adds `instantiations`.

### Query API (`src/db/symbol_database.h/.cpp`)

| Method | Description |
|---|---|
| `upsertFile`/`getFileHash` | stable file_id for a path; hash for cache-hit checks |
| `replaceSymbols`/`replaceDiagnostics`/`replaceImports`/`replaceInstantiations` | DELETE + INSERT per file, transactional |
| `appendDiagnostics` | INSERT-only (unlike `replaceDiagnostics`) — used by the library resolver so it doesn't wipe a file's own parse diagnostics |
| `symbolsForFile`/`diagnosticsForFile`/`importsForFileId` | by path/file_id |
| `findSymbolsByName`/`findSymbolsByNamePrefix` | cross-file; ordered `path, line` — no kind preference |
| `findSymbolsInScope(scope)` | exact scope match |
| `scopeAtPosition(path, line)` | innermost enclosing scope name, `""` if top-level |
| `findSymbolsVisibleAt(path, line)` | UNION ALL: (1) file-local scope chain, (2) cross-file top-level + wildcard-imported (+ transitively re-exported) package scopes, (3) one arm per specific import (+ re-exported specific items); **sorted in C++, not SQL** — SQLite disallows expressions like `length(scope)` in `ORDER BY` after `UNION ALL` |
| `collectExportedImports` *(private)* | cycle-safe recursive walk of `export pkg::*`/`export pkg::item` |
| `fileIdForPackage` *(private)* | file_id declaring a top-level package by name |
| `instantiationsOfType(typeName)` | every file referencing an unresolved instantiated type |
| `unresolvedInstantiatedTypeNames()` | distinct instantiated type names with no matching declaration anywhere — drives `LibraryResolver` |

`SymbolRow { id, kind, name, line, col, parent, detail, filePath, endLine, scope }`.

### `CompilationController` (`src/db/compilation_controller.h/.cpp`)

`compile(path, text, const ProjectConfig* config = nullptr)` — hash check → skip or
recompile → update DB. `config` seeds `SvPreprocessor`'s include dirs/defines; `nullptr`
(default) preserves pre-multi-file-project behavior exactly. Optional
`std::ostream* logStream` ctor param (default `nullptr`): when set, logs `[parsed] <path>`
(primary; ` (cached)` on a cache hit) and `[parsed]   included: <path>` per file in the
cache-miss loop — backs `--log-files`.

### Library dependency graph

```
svlsp_compiler (compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                parse_cache, filelist_parser, project_config, file_utils)
    → svlsp_antlr4

svlsp_db (database, symbol_database, compilation_controller, library_resolver, project_compiler)
    → svlsp_sqlite3 → svlsp_compiler

svlsp_lib (lsp/*, incl. project_manifest_parser, project_registry — needs lsp::json,
           not available to svlsp_compiler)
    → lsp → svlsp_compiler → svlsp_db
```

---

## Multi-file project support (Phase 6.2/6.3 — complete)

Two independent project-config formats both produce one `ProjectConfig`:
`.svlsp.json` manifest, and a VCS/Questa/Xcelium-style `.f` filelist. Any unsupported
`.f` switch is a **hard error**. Unresolved instantiations emit a diagnostic on the
referencing file.

- **`FilelistParser::parse(path, baseDir = "")`** (`src/compiler/filelist_parser.h/.cpp`):
  `-f FILE` recurses with the *same* baseDir (CWD-relative paths inside stay
  CWD-relative); `-F FILE` recurses with baseDir = that file's own parent dir.
  `baseDir=""` means "use CWD", matching pre-existing/CLI-tool behavior — **auto-discovery
  callers (`ProjectRegistry`) must pass the discovered config file's own parent dir
  explicitly**, or relative `-y`/bare-filename entries resolve against the server's CWD
  instead of the project (a real bug hit and fixed during Stage 6 — the two parsers are
  asymmetric here; `ProjectManifestParser` already resolves against its own dir
  internally). Cycle detection = active-recursion-stack (not permanent-visited) so
  diamond includes are legal (parsed twice, harmless — `compile()` is hash-cached).
- **`ProjectManifestParser::parse(path)`** (`src/lsp/project_manifest_parser.h/.cpp`):
  JSON via `lsp::json`. Unknown top-level keys silently ignored (opposite policy from
  the filelist parser). `"mode"` only accepts `"sv"`/`"v95"`. Relative
  `files`/`includeDirs`/`libraryDirs`/`libraryFiles` resolve against the manifest's
  own parent dir.
- **`LibraryResolver::resolve(config, controller, sdb)`** (`src/db/library_resolver.h/.cpp`):
  resolves one unresolved name at a time, re-querying `unresolvedInstantiatedTypeNames()`
  after every compile (simpler than a batch-per-round loop, same termination guarantee:
  failed names never retried, compiled paths never recompiled). `-v` files are indexed
  by a raw parse, not persisted, unless actually resolved against. `-y` search order:
  `libraryDirs` outer × `libExtensions` inner, first `readFile()` hit wins.
- **`ProjectCompiler::loadProject(config, controller, sdb)`** (`src/db/project_compiler.h/.cpp`):
  compiles every `config.files` entry (missing ones silently skipped), then invokes
  `LibraryResolver::resolve`.
- **`ProjectRegistry::configFor(filePath)`** (`src/lsp/project_registry.h/.cpp`): (1)
  explicit config path if set via `setExplicitConfigPath` — wins unconditionally; (2)
  upward filesystem search from the file's dir, checking `.svlsp.json`, `svlsp.json`,
  `.svlsp.f`, `svlsp.f`, `files.f` at each level; (3) `nullptr` if none found (today's
  single-file behavior, unchanged). **Cached by the discovered config file's own path**
  — first call parses + `loadProject`s, every later call for any file under that root
  returns the same cached pointer. A directory with no manifest in its ancestry is
  **not** cached negatively (redoes the cheap walk each time — not a bottleneck).
- **A real project's filelist/manifest must list every source file, including
  packages** — import/export resolution is a DB lookup by package name
  (`fileIdForPackage`) with no on-demand "go find this package's file" mechanism the
  way `LibraryResolver` does for module instantiations. Packages aren't
  instantiated, so it's easy to wrongly assume they don't need listing.

### Package import/export resolution (Phase 6.3 — complete)

`import pkg::*` / `import pkg::Foo` / `export pkg::*` / `export pkg::item` are tracked
(`ImportRecord` in `src/compiler/parse_record.h`, emitted by
`SvRecordListener::enterPackage_import_item`) and extend `findSymbolsVisibleAt`'s
UNION ALL (wildcard packages, specific items, plus transitively re-exported symbols via
`collectExportedImports`, cycle-safe). **Plain (non-`export`) imports are never
followed transitively.** The LRM's `export *::*;` shorthand (re-export everything
imported into current scope) is **not implemented** — that literal doesn't route
through the `package_import_item` grammar rule at all.

---

## Preprocessor (`src/compiler/`)

Two-pass pipeline: **pass 1** (`CompilerDirectiveStripper::strip`) resolves
`` `__FILE__ ``/`` `__LINE__ `` against the original source and strips metadata
directives (runs once per file, including recursively for each `` `include ``d file,
so `` `__FILE__ ``/`` `__LINE__ `` inside includes resolve correctly); **pass 2**
(`SvPreprocessor::process`) handles `` `define ``, `` `ifdef ``, `` `include ``, macro
invocation/expansion.

**Source map** (`sourceMap: vector<SourceLine>`, returned alongside expanded text):
maps each output line back to `{file, line}` (`file==""` = primary file) plus
`colShifts: vector<ColShift>` (`{outputCol, delta}` breakpoints) for column drift from
mid-line macro expansion. `SvTreeWalker::walk` uses `translateLine`/`translateColumn`
to convert every `ParseRecord`/`ParseError` back to original-file coordinates before
they leave the compiler layer.

**Supported:** function-like macro default argument values (`` `define M(A,
B=expr) ``, arbitrary-expression defaults incl. nested calls); `{}`/`[]`-aware (and
string-literal-aware) macro-argument splitting; multi-line macro *invocations* without
backslash continuation (paren/brace/bracket balance tracked across physical lines) and
multi-line `` `define `` *bodies* via backslash continuation; `//` comments are never
scanned for macro invocations; stringification (`` `"..."`" ``, recursively expands
macro references inside the span before quoting); token-pasting (` `` `, textual splice
— deletes ` `` ` and adjacent whitespace, splicing surrounding text; run before
`expandStr` scans, so it transparently handles pasting into a new macro-invocation name
and pasting nested inside a stringification span).

**Not supported (by design):** token-pasting where an operand is itself an *unexpanded*
nested macro invocation whose expanded (not literal) result is needed — matches C's
`##` behavior; the `` `ifdef ``/`` `else ``/`` `endif `` directives *inside* a
`` `define `` body are not evaluated at expansion time — both branches' text end up
concatenated unconditionally (real UVM site: `` `m_uvm_field_op_begin ``,
`macros/uvm_object_defines.svh:826-831` — low real-world impact, only reachable via
field-automation macros; not confirmed as a corpus contributor beyond that one site).

---

## Working rules

- Every function has a unit test before the implementation is written.
- Every LSP feature needs **both** a unit test and a functional Emacs test — neither
  alone is sufficient.
- Two commits per feature: `feat(<module>): ...` then `docs(<module>): ...`.
- `main` branch is always green (unit + functional tests passing).
- Only commit when the user explicitly asks.

## Key decisions

| Decision | Choice |
|---|---|
| LSP framework | lsp-framework v1.3.1 (submodule) |
| Unit test framework | Catch2 v3.8.1 (FetchContent) |
| Transport | stdio |
| Compiler | g++-13 |
| Parser generator | ANTLR4 v4.13.2 (FetchContent) |
| Database | SQLite3 (amalgamation, schema v5) |
| SV preprocessor | Minimal in-house C++ (not slang) |
| `__FILE__`/`__LINE__` | Resolved in pass 1, before include shifts line numbers |

---

## Emacs test infrastructure

`tools/emacs-test-daemon.sh`: installs lsp-mode from MELPA into `.emacs-test/` on
first run (stamp file skips re-download), starts an `emacs --daemon`, sources
`tools/emacs-test-lib.sh`, runs each `test_*.sh` argument, reports pass/fail.
`tools/emacs-test-init.el` registers svlsp for `verilog-mode`.

- All Elisp passed to `emacsclient` must be wrapped in `condition-case` — the daemon
  script uses `set -euo pipefail`, an uncaught Elisp error kills the script.
- `(lsp-workspaces)` is buffer-local — call inside `with-current-buffer`.
- `lsp-request` returns `nil` for a JSON null response.
- **The entire Emacs daemon session shares one svlsp server process and one growing
  in-memory DB across every `test_*.sh` file.** Any new fixture's symbol names must be
  checked against the whole `tests/integration/fixtures/**` + `examples/**` tree, not
  just its own file — a name collision with an unrelated fixture will make
  hover/definition ambiguous.

---

## UVM corpus testing (`tests/uvm_corpus/`)

Opt-in, separate CMake target `uvm_corpus_tests` (not registered with `ctest`/`make
test` — deliberately, since it depends on an external ~140-file UVM checkout not
tracked by this repo's git). Covers the 5 DB-backed LSP features (hover, definition,
documentSymbol, workspace/symbol, completion) against a real, full compile of UVM.
Corpus root resolves from `SVLSP_UVM_CORPUS_DIR` env var. If
references/rename/signatureHelp are ever upgraded from stub to DB-backed, add
corresponding `test_*_uvm.cpp` files here.

**Real-world result of all grammar/preprocessor fixes below:** the 140-file corpus
went from 2956 diagnostics (session 3 baseline) to **1 remaining diagnostic** — see
"data_type ambiguity" in Known gaps below for what that one is.

**URI subtlety for this suite specifically:** `` `include ``d files are stored under
bare corpus-root-relative paths, but `lsp::DocumentUri::fromPath()` always
absolutizes and can't represent a bare relative path verbatim. Fixture helpers
`uriForRelPath` (input params — `lsp::Uri::parse("file:" + relPath)`, no filesystem
access) vs. `expectedUriPath` (output comparison — runs the expected value through
`FileUri::fromPath()` first) handle this; see `tests/uvm_corpus/uvm_corpus_fixture.h`.

---

## Sv.g4 grammar quirks

| Construct | Issue | Status |
|---|---|---|
| Backtick directives (`` `define ``, `` `ifdef ``, `` `timescale ``) | Not in grammar at all | Must be preprocessed first (by design) |
| `bind M C u (.p(p));` | `bind_directive` adds its own `';'` on top of `module_instantiation`'s | Write `;;` |
| `bind` with parameter override (`#(...)`) | LL(*) prediction fails | Omit override; use default params |
| `cross A, B { ignore_bins x = ...; }` | Double semicolon from `cross_body_item` + `cross_body` | Use `cross A, B;` (empty body) |
| `` `timescale `` | No backtick directive support | Use `timeunit`/`timeprecision` inside module |
| String literal escapes (`\"`) | **Fixed** — `STRING_LITERAL` now `'"' ('\\' . | ~["\\])* '"'` | — |
| `void'(f())` cast | **Fixed** — `SINGLE_QUOTE?` made optional in `subroutine_call_statement` | — |
| `#0;` bare zero-delay statement | **Fixed** — was colliding with an implicit `'#0'` literal token from assert syntax; now `'#' DECIMAL_NUMBER` | — |
| Method named `sample()` | **Fixed** — was colliding with `coverage_event`'s `'sample'` literal; now `IDENTIFIER` | — |
| Empty assignment pattern `` '{} `` | **Fixed** — added as a 4th `assignment_pattern` alternative | — |
| `const` class property initialized with `new(...)` | **Fixed** — `class_property`'s `('=' constant_expression)?` widened to `('=' expression)?` | — |
| `class_scope`d chained call, `` X::Y::method(args) `` (e.g. UVM's `T::type_id::create(...)` factory idiom) | **Fixed** — `ps_or_hierarchical_tf_identifier` gained a `class_scope tf_identifier` alternative | — |
| SV token-pasting (` `` `) inside `` `define `` bodies | **Fixed** — see preprocessor section above | — |
| Stringification (`` `"..."`" ``) | **Fixed** — see preprocessor section above | — |
| **`data_type`/`variable_decl_assignment` ambiguity** (`grammar/Sv.g4:740-753`, alts 9/10/12 all reduce to a bare `IDENTIFIER` — SV's classic "identifier classification needs a symbol table" problem, LRM Annex A acknowledges this) | **Confirmed, not fixed.** Under default (SLL) prediction this is silently resolved correctly almost everywhere; fails specifically for `const local`/`const protected` (or any 2+ qualifiers) + `new(...)` initializer combos. Real fix needs semantic predicates (symbol table) or risky restructuring of some of the grammar's most heavily-used rules — not attempted; two cheap structural experiments (reordering alts, removing a redundant one) had no effect. Real-world impact: **1 diagnostic in the entire 140-file UVM corpus** (`base/uvm_transaction.svh`). | Open — revisit only if it starts showing up more broadly |

---

## Known gaps / things to watch out for

- `handleExit` doesn't distinguish clean vs. abnormal exit code; `run()` always returns 0.
- `m_parentProcessId` stored but unused (reserved for parent-process monitoring).
- Emacs test harness needs a display (`Xvfb :99 &; DISPLAY=:99 make test-integration`).
- `CompilationController` uses `":memory:"` SQLite — symbols lost on server restart
  (swap to a file-backed path in `server.cpp` for persistence, straightforward change).
- `export *::*;` LRM shorthand not implemented (see Phase 6.3 section above).
- **References, rename, signature help are unconditional-null stubs.** No real
  implementation exists yet.
- **LSP diagnostics-visibility gap**: only the primary opened file's diagnostics are
  ever `publish()`'d to the client; included files' diagnostics are computed/persisted
  to the DB but never sent. See "Document store / diagnostics" above.
- ~~`findSymbolsByName`/hover/definition have no kind-preference tiebreak~~ —
  **mitigated 2026-09-01** via `pickBestSymbol()` (see above): declaration-like kinds
  (`Module`/`Interface`/`Program`/`Package`/`Class`/`Function`/`Task`) now outrank
  data-like kinds (`Signal`/`Port`/`Parameter`/`Macro`) when no same-file match exists.
  This previously caused a reproducible wrong-hover bug in real UVM (a
  token-pasting-corrupted `Signal` symbol alphabetically outranked the real `Class` of
  the same name) — that specific case was already fixed at the source (token-pasting
  works) and this closes the general risk class. Residual risk: two symbols of the
  *same* kind-tier with no same-file match still fall back to plain
  `ORDER BY path, line` (e.g. two same-named `Signal`s in different files) — not
  pursued further since no real-world case has surfaced.
- **Fix 4's `expression`-widening perf cost** (the `const` class-property change above):
  full UVM corpus compile went from ~14min/~1.3GB RSS to ~17min/~6.7GB RSS after this
  fix, working theory being `expression` is a much larger/more recursive rule than
  `constant_expression`, combined with the `data_type` ambiguity above triggering more
  ANTLR full-context prediction fallback. Not profiled/confirmed. Worth narrowing
  (e.g. a `class_new`-inclusive alternative instead of fully general `expression`) if
  this matters at real-world (Phase 6.5 performance) scale.
- ANTLR parse performance at real-world scale (large real SV class bodies, e.g.
  `uvm_component.svh` at 3780 lines) has an inherent, roughly-linear-but-high-constant
  per-line cost independent of any preprocessor gap — Phase 6.5 territory, not
  attempted (would need profiling, possibly grammar restructuring).
- ~~`didChange` recompiles synchronously on the message-read thread, every time,
  with no debouncing~~ — **fixed 2026-08-28**, see "Debounced `didChange`" under
  "I/O wrapper" above (`plan.md §6.8`). One deliberate deviation from that
  section's original sketch: the implementation does *not* use the framework's
  `lsp::AsyncNotificationResult`/`ThreadPool` machinery — `didChange`'s handler
  already returns immediately after `schedule()`, so `ChangeDebouncer`'s own
  worker thread is sufficient to get the actual compile off the message-read
  thread without needing that extra layer. Remaining open point from the
  original design, still true: the 300ms debounce delay is a fixed constant,
  not configurable via `initializationOptions` — see "Not yet done" #10 below.

---

## Not yet done — next steps

Roughly in suggested priority order; none are blocking, pick based on what matters most:

1. **Phase 6.4 — cross-file invalidation / dependency graph** (`plan.md §6.4`) — not
   yet planned in file-level detail. Needed for correct incremental recompilation when
   a shared/included file changes.
2. **LSP diagnostics-visibility gap** — publish diagnostics for every file touched by
   a `compile()` call, not just the primary opened one. Small, self-contained,
   immediately-actionable editor-UX fix independent of Phase 6.4.
3. **Implement real `references`/`rename`/`signatureHelp`** — currently unconditional
   null stubs; if upgraded, add matching `tests/uvm_corpus/test_*_uvm.cpp` coverage too.
4. **`data_type`/`variable_decl_assignment` ambiguity** — documented, not fixed (see
   grammar quirks table above). Only pursue if it starts causing more than the current
   1 known corpus diagnostic, or a user-reported false diagnostic traces back to it.
5. **Fix 4 performance cost** — consider narrowing the `const` property initializer
   grammar rule if full-corpus/real-world compile time becomes a problem.
6. **Gap E** (`` `ifdef ``/`` `else ``/`` `endif `` inside `` `define `` bodies) —
   still not confirmed as a real contributor beyond the one known
   `` `m_uvm_field_op_begin `` site. Re-check against corpus diagnostics if it ever
   seems to resurface.
7. ~~Consider a `findSymbolsByName`/hover disambiguation improvement~~ — **implemented
   2026-09-01**: `pickBestSymbol()` (`src/lsp/symbol_utils.h/.cpp`), used by both
   `HoverProvider` and `DefinitionProvider`, now falls back to preferring a
   declaration-like kind (`Module`/`Interface`/`Program`/`Package`/`Class`/`Function`/
   `Task` over `Signal`/`Port`/`Parameter`/`Macro`) when no same-file match exists,
   before falling back to the first `path,line`-ordered row. The "exclude symbols from
   files with a diagnostic at that exact line" alternative was not pursued — kind
   preference is simpler and covers the real UVM bug's shape directly.
8. ~~Consider broadening `tests/uvm_corpus/test_completion_uvm.cpp`'s "first cut"
   2-scenario coverage~~ — **done 2026-09-01**: 2 more scenarios added (now 4,
   193 assertions), closing the two gaps the original pair never touched — an
   empty typed prefix (the `rows.empty()` unranked-full-list path) and true
   fuzzy/skip-tolerant scoring (vs. only exact/contiguous-prefix filtering),
   both now proven against real, full-corpus-scale data for the first time.
   Surfaced a real scoping subtlety worth knowing for any future corpus test:
   every real UVM class lives inside `package uvm_pkg`, so its symbol's own
   `scope` column is `"uvm_pkg"`, never `""` — `findSymbolsVisibleAt`'s
   cross-file union only ever matches scope `""`, so a symbol from one corpus
   file is genuinely invisible to a completion request from another corpus
   file unless that requesting file has its own explicit
   `import uvm_pkg::*` (files *inside* the package, like `uvm_component.svh`
   itself, don't import themselves). All 4 scenarios stayed anchored on
   `base/uvm_component.svh`'s own same-file symbols (including out-of-class
   `uvm_component::method_name(...)` body definitions, which — despite
   sitting textually after `endclass` — are still inside the enclosing
   `package uvm_pkg`, so they remain visible from anywhere else in that same
   file) to sidestep that gap rather than exercise real cross-file
   completion, which would need a fixture with an actual import.
9. Minor: a short code comment at `pathToUri()` (`src/lsp/symbol_utils.cpp`) explaining
   the `fromPath()`-always-absolutizes subtlety, for the next person constructing a URI
   for a non-`didOpen`ed file.
10. ~~Debounced/async `didChange` compilation~~ (`plan.md §6.8`) — **implemented
    2026-08-28** (commit `d17855a`): `ChangeDebouncer` + `m_dataMutex` + full-stack
    regression test, see "Debounced `didChange`" under "I/O wrapper" above. What's
    left, low priority: the 300ms debounce interval is a fixed constant, not
    configurable via `initializationOptions` — the plan's own open question on this
    said "start with a fixed constant; revisit only if real usage shows one value
    doesn't suit both small and very large files," which hasn't happened yet.
    Now planned as `plan.md §6.12` — see #14 below.
11. **Keyword completion (`plan.md §6.9`)** — not started. Reserved SV words
    (`always_ff`, `logic`, `endmodule`, etc.) never appear as completion
    candidates today; `CompletionProvider::getCompletion` only ranks DB rows
    from `findSymbolsVisibleAt`. Needs a keyword list (hand-maintained from
    LRM Annex B, or script-extracted from `grammar/Sv.g4`'s scattered string
    literals), merged into the same fuzzy-scored candidate set, plus a fix to
    the `rows.empty()` early return so an empty/new buffer still offers
    keywords. See plan.md for full detail.
12. **Dot / member-access completion (`plan.md §6.10`)** — not started.
    `foo.` should narrow completion to `foo`'s type members, but nothing
    captures a declared variable's type today (`enterData_declaration` in
    `src/compiler/sv_tree_walker.cpp` leaves `Signal` records' `detail`
    empty), so `getCompletion` has no way to resolve `foo` to a class/struct
    scope and fall back to `findSymbolsInScope`. Needs: (1) populating
    `detail` with the declared type for class/interface/struct/typedef-typed
    `Signal`/`Port`/`Parameter` declarations, (2) a dot-aware extension to
    `wordAtPosition` to recover the object expression before the cursor, (3)
    a new branch in `CompletionProvider::getCompletion` that resolves the
    object's type and queries `findSymbolsInScope` instead of
    `findSymbolsVisibleAt`. First cut is single-segment (`foo.bar`) only —
    chained access (`foo.bar.baz`) and `this`/`super` deferred. See plan.md
    for full detail.
13. **Configurable fuzzy-matching toggle (`plan.md §6.11`)** — not started.
    `CompletionProvider::getCompletion` always fuzzy-scores when a prefix is
    typed, with no way to opt out. Needs an `initializationOptions.svlsp.
    fuzzyCompletion` boolean (default true, absent/wrong-type = default —
    same pattern as `svlsp.projectConfig` in `ServerState::
    extractProjectConfigPath`), threaded through as a new `getCompletion`
    parameter; disabled mode restores the pre-fuzzy strict-prefix, DB-order,
    unranked behavior. Fixed at `initialize`, not live-reconfigurable — no
    `workspace/didChangeConfiguration` handling exists in this codebase.
    See plan.md for full detail.
14. **Configurable debounce interval (`plan.md §6.12`)** — not started.
    Resolves §6.8's own open question. `ChangeDebouncer`'s 300ms delay
    (`src/lsp/change_debouncer.h:26-27`) is a hardcoded default, never
    overridden by `LanguageServer`. Needs an `initializationOptions.svlsp.
    debounceMs` integer (same absent/wrong-type-means-default pattern as
    `projectConfig`/§6.11's flag). **Real blocker:** `m_debouncer` is
    constructed in `LanguageServer`'s member-initializer list
    (`src/lsp/server.cpp:15-17`) — at process startup, before `initialize`
    (and thus `initializationOptions`) exists — so the value can't just be
    passed to `ChangeDebouncer`'s constructor. Plan recommends adding a
    `ChangeDebouncer::setDelay()` mutator (guarded by its existing mutex)
    called from the `initialize` handler, over restructuring `m_debouncer`
    into a lazily-constructed member. See plan.md for full detail.
15. **Performance §6.5 — two new open questions; prior art confirmed
    2026-08-28, integration design still not started:**
    - (a) A long-lived, pre-warmed `svlsp` daemon (started once, e.g. every
      morning, ahead of any editor session, potentially shared by multiple
      editor windows) to help both cold-start latency and §6.8's
      debounce/compile load. **Not a novel idea** — Bazel and Buck2 already
      work this way (persistent per-workspace daemon, Buck2 shares one daemon
      across clients keyed by workspace identity, directly analogous to
      "multiple editor windows share one warm `svlsp`"). Weigh against the
      simpler on-disk-DB-persistence option listed right above it in
      `plan.md` before building this.
    - (b) A pre-built, reusable DB for rarely-changing library code (UVM,
      verification IP — a large fraction of a real project's files, shared
      unchanged across many projects), instead of every project recompiling
      it from scratch. **Also not novel — two confirmed real-world models to
      copy rather than reinvent:** clangd's background index (per-translation-
      unit shards persisted on disk, stitched together at query time via a
      `MergedIndex` — no physical merge, no shared key space; this is the
      "attach-and-query" shape) and LSIF/its successor SCIP (pre-built once
      per project/dependency version, combined by linking on stable
      *monikers* — content/identity-derived keys — rather than any
      per-dump surrogate key; this solves the exact `files.id`/`symbols.id`
      autoincrement-collision problem a true merge would hit here, confirming
      a path+hash-style key would sidestep it). **Recommended default:**
      model on clangd's query-time merge; only borrow the LSIF/SCIP
      moniker-key idea if a true physical merge turns out to be necessary
      (e.g. for a single-file distribution format). Note
      `SQLITE_MAX_ATTACHED`'s default limit of 10 attached databases per
      connection if the attach-based shape is pursued with many separate
      library DBs. The still-open, no-prior-art part is entirely `svlsp`-
      specific: reshaping `SymbolDatabase`'s existing queries (§5.3) to query
      across an attached library DB, and the library-versioning/pinning/
      staleness story (config field, where pre-built DBs are built/shipped).
