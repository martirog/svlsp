# svlsp — Handoff Document

**Last updated:** 2026-09-24.

This file covers only **current state, what's next, and what you need to know
to work in the repo**. The full design and implementation history of every
finished section lives in `plan.md` (each §6.x has its own **Status:** line and
write-up) — read the relevant section there rather than looking for it here.

---

## Current state

- Everything through plan.md **§6.28** is committed
  (`4378a29` code+tests, `6d63659` docs). `a01acb4` added the **§6.29** plan
  (docs only, nothing implemented). `ad85a28` added scope-strict
  definition/references tests; §6.30 plans the fix (see "Other open work"
  item 0).
- Test baselines (all green at last run):
  - unit: **726 cases** — 700 pass + 26 `[!shouldfail]` known gaps
    (`build/debug/unit_tests`)
  - Emacs functional: **222/222** across 41 `tests/integration/test_*.sh` files
  - UVM corpus (opt-in): **505 assertions / 14 cases**
  - UVM corpus `--build-db`: **1 diagnostic** total (the known
    `data_type` ambiguity, see "Grammar quirks")

## Next up — plan.md §6.29 (not started)

Signature help for three call shapes that currently return null. See §6.29 for
the full design; summary:

- **A. Macros** (`` `uvm_info(ID, MSG, VERB) ``) — depends on §6.25's
  investigation: retain `MacroRecord`'s parameter list, persist
  `ParseRecordKind::Macro` rows, detect `` `name( `` in the unexpanded buffer.
  Note `` ` `` is not an ident char in `signature_help.cpp`, so today a macro
  call is read as a bare name and could falsely match a same-named function.
- **B. Keyword constructs** (`for`/`foreach`/`case*`/`assert`/
  `randomize … with`, …) — static table; `for` needs `;` as separator. Which
  keywords are worth it is an open question.
- **C. System tasks/functions** (`$display`, `$fatal`, `$clog2`, …) — static
  `src/lsp/sv_system_tasks.h` table as a fallback after the DB lookup fails
  (`isIdentChar` already accepts `$`); variadic clamping of
  `activeParameter`. Most self-contained part — a good place to start.
- Shared: lookup order macro → keyword → instantiation → bare call (DB, then
  `$` table) → dotted call; update `signature_help.h`'s header comment;
  consider advertising `triggerCharacters = {"(", ","}` in
  `server_state.cpp` once all land.

## Other open work (priority roughly top-down)

0. **Definition and references aren't scope-aware — fix planned as
   plan.md §6.30 (tests committed `ad85a28`).** `DefinitionProvider` is `wordAtPosition` →
   `findSymbolsByName` → `pickBestSymbol`; `ReferencesProvider` is a
   lexical whole-word search. New real-compile suites pin this down with
   same-named decoys and exact locations:
   `tests/unit/lsp/test_definition_scoped.cpp` (23 cases; 17 known gaps:
   `pkg_b::X`, `Class::m()`, imports, `obj.m()`/`obj.f`, inheritance,
   chains, `this.`/`super.`, local shadowing) and
   `tests/unit/lsp/test_references_scoped.cpp` (27 cases; every recorded
   kind passes exactly, 9 known gaps: same-named decoys in other
   modules/classes, and typedef / enum literal / struct member / genvar /
   macro, which aren't recorded as symbols at all so references return
   null). Known gaps are tagged `[!shouldfail]` — they flip to failures
   once fixed, so drop the tag then. Functional round trips for the
   passing cases: `tests/integration/test_40_scoped_definition_references.sh`
   (`fixtures/defref_top.sv` + `defref_inc.svh`). A fix would reuse
   §6.26/§6.27's `resolveChain`/`resolveMethod` and `findSymbolsVisibleAt`;
   §6.30 also covers a newly found same-file wildcard-import visibility bug
   and out-of-class method bodies (item 2).
1. **`wordAtPosition` resolves symbols inside comments/strings — real bug, not
   in plan.md yet.** Hovering `put` in `endfunction // put`
   (`/home/martin/src/policy/policy_mixin.sv:37`) returns an unrelated
   `uvm_cache` method. `wordAtPosition` (`src/lsp/symbol_utils.cpp`) has no
   comment/string awareness, unlike `findIdentifierOccurrences` in the same
   file. Affects hover, definition, completion, references, rename. Needs unit
   tests for `//`, `/* */`, and string-literal cursors (each → nothing) plus a
   functional test.
2. **Out-of-class method bodies aren't scoped under their class**
   (`function void C::m(); … endfunction`) — members of `C` are invisible to
   `findSymbolsVisibleAt` inside the body. Affects hover/completion/definition/
   signature help. Disclosed in plan.md §6.27; not fixed or planned yet.
3. **§6.21** semantic reference-resolution diagnostics (unresolved type/import)
   — not started. Related real bug: a live edit's `replaceDiagnostics` wipes
   diagnostics `LibraryResolver` appended earlier (one undiscriminated
   `diagnostics` table).
4. **§6.24** audit compiler warnings from `tools/build.sh` — not started.
5. **§6.12** configurable debounce interval (`debounceMs`) — not started.
6. **§6.20** external read-only DB access — not started.
7. **§6.22 follow-up**: UVM-corpus coverage for references/rename.
8. **§6.19** library-DB staleness detection (source changed since build) —
   open question, no shape chosen.
9. **§6.5** performance — open; see "Known gaps" for the concrete costs.
10. Comma-shorthand parameters (`function f(input int a, b)`) drop `b` — a
    grammar ambiguity in `tf_port_item`; causes positional misalignment in
    §6.23's missing-argument check. Not fixed.
11. Minor: comment at `pathToUri()` explaining that `fromPath()` always
    absolutizes.

---

## What this project is

A SystemVerilog LSP server in C++20: lsp-framework protocol layer, ANTLR4
front-end (`grammar/Sv.g4`), SQLite symbol DB, and an Emacs daemon +
lsp-mode as the automated functional-test client. `plan.md` is the full
phased plan.

## Repository layout

```
src/lsp/           server, server_state, document_store, diagnostics, change_debouncer,
                   hover, definition, references, rename, completion, fuzzy_match,
                   signature_help, document_symbols, workspace_symbols, symbol_utils,
                   sv_keywords.h, sv_builtin_methods.h, project_manifest_parser,
                   project_registry, library_db_builder
src/compiler/      compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                   parse_record, parse_cache, filelist_parser, project_config, file_utils
src/db/            database, symbol_database, compilation_controller,
                   library_resolver, project_compiler, schema (v8)
src/main.cpp       entry point (--version, --log-files, --build-db/--output)
tests/unit/        Catch2 unit tests
tests/integration/ Emacs functional tests + fixtures/
tests/uvm_corpus/  opt-in suite against an external UVM checkout (not in ctest)
tools/             build.sh, emacs-test-daemon.sh, emacs-test-init.el, emacs-test-lib.sh
examples/          .sv example files (also loaded by functional tests)
docs/              per-phase docs, usage.md (end-user config/CLI), decisions/
```

## Build and test

```bash
cmake --preset debug && cmake --build --preset debug     # or: make configure build
tools/build.sh [debug|release] [--target NAME] [--output-dir DIR]   # from-scratch build

build/debug/unit_tests                        # unit tests
build/debug/unit_tests "[compiler][parser]"   # subset by tag

DISPLAY=:99 make test-integration             # Emacs functional suite (needs Xvfb :99)
bash tools/emacs-test-daemon.sh tests/integration/test_12_signature_help.sh

cmake --build --preset release --target uvm_corpus_tests && ./build/release/uvm_corpus_tests
                                              # corpus root: SVLSP_UVM_CORPUS_DIR
                                              # (default /home/martin/src/verilator_test/uvm-core/src)

build/release/svlsp --build-db <config.f|.svlsp.json> --output <lib.db>
build/release/svlsp --log-files /path/to/log.txt
```

Compiler **g++-13** (pinned in `CMakePresets.json`). Debug has ASan+UBSan
(10-100x slower) — use release for anything corpus-sized.

**Traps that have bitten before:**
- **Rebuild the `svlsp` executable before trusting an Emacs result.**
  `--target svlsp_lib`/`unit_tests` does not relink `build/debug/svlsp`, which
  is what the Emacs harness runs. Stale binaries have produced both false
  passes and false failures.
- `~/bin/svlsp` (from `tools/build.sh … --output-dir ~/bin`) is first on the
  user's `PATH` and is only refreshed when `--output-dir` is re-passed. If it's
  in use by a live editor, `cp` fails ("Text file busy") — swap with `mv`.
- `--build-db` forces a full rebuild when the DB's recorded
  `library_build_info` version differs from the running binary's git version;
  same version reuses the per-file hash cache.

## Working rules

- Unit test first, then implementation. Every LSP feature needs **both** a unit
  test and an Emacs functional test.
- Commit only when the user asks. Standing pattern: a `feat(...)` code+tests
  commit, then a separate `docs(...)` commit (plan.md + handoff.md) — confirm
  before assuming.
- Keep `main` green (unit + functional).
- Verify empirically (live `svlsp` probe, real corpus, real fixture) rather
  than inferring behavior from reading `Sv.g4` — ANTLR's chosen alternative has
  surprised us more than once.

## Key decisions

| Decision | Choice |
|---|---|
| LSP framework | lsp-framework v1.3.1 (submodule) |
| Unit tests | Catch2 v3.8.1 (FetchContent) |
| Parser | ANTLR4 v4.13.2 (FetchContent) |
| Database | SQLite3 amalgamation, schema v8 |
| Preprocessor | in-house C++ (not slang) |
| Transport | stdio |

---

## Architecture quick reference

### Server (`src/lsp/server.cpp`)

- `m_db` is `":memory:"` — repopulated per `didOpen`/`didChange`.
- `didChange` → `ChangeDebouncer` (300ms, own worker thread) →
  `compileAndPublish`. `didSave` cancels the pending debounce and compiles
  synchronously, then queues every `` `include ``r of the saved file on a
  second debouncer (`m_dependencyRechecker`, `forceRecompile=true`).
- One coarse `m_dataMutex` guards store/DB/compiler/registry; never held
  across a whole dependency cascade.
- Primary file's diagnostics come from `compile()`'s return value; included
  files' diagnostics are read back from the DB and published with no
  `version`. Anything appended to the DB during `compile()` must also be merged
  into the return value to reach the client.
- `ServerCapabilities` designated initializers must follow declaration order.

### Providers

| Provider | Resolution |
|---|---|
| Hover / Definition | `wordAtPosition` → `findSymbolsByName` → `pickBestSymbol` (same file, then declaration-like kind, then `path,line`) |
| Completion | `findSymbolsVisibleAt` + keywords (`sv_keywords.h`, scope-kind legality) → fuzzy scoring (toggle: `initializationOptions.svlsp.fuzzyCompletion`). Dot-completion: `dotCompletionContext` → `resolveChain` → `membersAcrossChain` (full `extends` walk), builtin container methods via `sv_builtin_methods.h` |
| References / Rename | lexical cross-file `findIdentifierOccurrences` — not scope-aware |
| Signature help | instantiation ports; bare / `Class::` / `pkg::` calls (`resolveMethod` in enclosing class hierarchy, then flat fallback for unscoped only); dotted calls (`parseDottedCallHeader` → `resolveChain` → `resolveMethod`). Labels from `Port.detail` (`"<dir> <type>"`, optional `\x1F<default>` suffix) |

Chain-resolution helpers (`resolveChain`, `membersAcrossChain`,
`peelDimensionLayers`, `dotCompletionContext`, `positionForOffset`) live in
`src/lsp/symbol_utils.h/.cpp`, shared by completion and signature help.
`svlsp_db` must not depend on `svlsp_lib`, which is why
`compilation_controller.cpp` carries its own small `pickCallee`.

### Encodings in `symbols.detail`

- Class: single parent class name (`extends`); `SymbolDatabase::baseClassChain`
  walks it (cycle-guarded).
- Signal/Parameter: `:`-joined type layers, outermost first, with `$`-prefixed
  container tags (`$queue`, `$assoc_array`, `$dynamic_array`, `$fixed_array`,
  `$string`, `$event`) — e.g. `"$fixed_array:$queue:MyClass"`.
- Port: `"<direction> <type>"`, optionally `"\x1F<default>"`
  (`PARAM_DEFAULT_VALUE_SEP`); interface ports store only their header text.
- `""` as a *scope* means top level — `findSymbolsInScope("")` returns every
  top-level symbol, so guard against an empty resolved type.

### Database (`src/db/`, schema v8)

Tables: `files`, `symbols (kind, name, line, col, parent, detail, end_line,
scope)`, `diagnostics`, `imports (pkg_name, item, is_export)`,
`instantiations`, `library_include_dirs`, `library_build_info`,
`file_includes`. Migrations run automatically in `database.cpp`.

Key queries: `findSymbolsByName`/`ByNamePrefix`/`InScope` (union across attached
library DBs), `findSymbolsVisibleAt` (local scope chain + top-level + wildcard/
specific imports + re-exports; sorted in C++ since SQLite can't order
expressions after `UNION ALL`), `scopeAtPosition`/`scopeKindAtPosition`/
`enclosingClassNameAt`, `baseClassChain`/`resolveMethod`, `includersOf`,
`appendDiagnostics` vs `replaceDiagnostics`.

Library DBs (§6.19): `--build-db` builds one; `libraryDbs` attaches read-only
(`lib0`, `lib1`, …; SQLite's default limit is 10 attached DBs);
`libraryDbSources` builds-and-caches on first use; the DB's own `includeDirs`
are baked in and merged into the referencing project before preprocessing.
Several queries (`scopeAtPosition`, `symbolsForFile`, `diagnosticsForFile`,
instantiation queries) remain main-schema-only.

### Projects and packages

- Configs: `.svlsp.json` (unknown keys ignored) or `.f` filelist (unknown switch
  = hard error). `ProjectRegistry::configFor` = explicit
  `initializationOptions.svlsp.projectConfig`, else upward search for
  `.svlsp.json`, `svlsp.json`, `.svlsp.f`, `svlsp.f`, `files.f`.
- Auto-discovery must pass the config file's own dir as `baseDir` to
  `FilelistParser`; empty baseDir means CWD for both parsers.
- A project must list **every** file including packages — imports resolve by
  DB lookup only; only module instantiations are resolved on demand
  (`LibraryResolver`, `-v`/`-y`).
- Plain imports are never followed transitively; `export pkg::*`/`pkg::item`
  are. `export *::*;` is not implemented.
- Every UVM class is scoped `uvm_pkg`, so it's invisible cross-file without an
  explicit `import uvm_pkg::*`.

### Preprocessor

Pass 1 (`CompilerDirectiveStripper`) resolves `` `__FILE__ ``/`` `__LINE__ ``;
pass 2 (`SvPreprocessor::process`) does `` `define ``/`` `ifdef ``/
`` `include ``/expansion (defaults, stringification, token-pasting, multi-line
invocations), producing a `sourceMap` + column shifts that `SvTreeWalker` uses
to map records back to original coordinates. Not supported: `` `ifdef `` inside
a `` `define `` body is not evaluated (both branches concatenated — one known
UVM site, `` `m_uvm_field_op_begin ``); token-pasting on an unexpanded nested
macro's result.

---

## Emacs test infrastructure

- `tools/emacs-test-daemon.sh` installs lsp-mode into `.emacs-test/` once, runs
  each `test_*.sh` against one daemon.
- **One svlsp process and one growing in-memory DB are shared across every test
  file** — give fixture symbols a unique prefix (`rrsh_`, `gapb_`, `diagvis_`,
  …) and check for collisions across `tests/integration/fixtures/**` and
  `examples/**`.
- Append new signature-help fixtures to the **end** of
  `fixtures/ref_rename_sighelp.sv` so existing hardcoded line numbers stay valid.
- Wrap all Elisp in `condition-case` (the script is `set -euo pipefail`).
  `(lsp-workspaces)` is buffer-local. `lsp-request` returns `nil` for JSON null.
- Timing-sensitive tests: set `lsp-idle-delay` **before** `(lsp)` connects.
  Use `lsp-workspace-shutdown`, not `lsp-workspace-restart`, when a test needs
  a fresh `initialize`.

## UVM corpus testing

`uvm_corpus_tests` target, deliberately not in ctest. Covers hover,
definition, documentSymbol, workspace/symbol, completion, and signature help
against a full UVM compile. `` `include ``d files are stored under
corpus-relative paths — use `uriForRelPath`/`expectedUriPath` from
`tests/uvm_corpus/uvm_corpus_fixture.h`, not `FileUri::fromPath` (always
absolutizes).

## Grammar quirks still open (`grammar/Sv.g4`)

| Construct | Status |
|---|---|
| `data_type`/`variable_decl_assignment` ambiguity (bare `IDENTIFIER` in several alts) — fails for 2+ qualifiers + `new(...)` initializer | Not fixed; 1 corpus diagnostic (`base/uvm_transaction.svh`) |
| Bare `MyClass foo;` parses as `net_declaration` (nettype), not `data_declaration` | Handled in both listeners; ambiguity itself unchanged |
| `bind … ;` needs `;;`; `bind` with `#(...)` override fails | Workaround |
| `cross A, B { … }` double semicolon | Use empty body |
| No backtick directives in grammar | By design — preprocessed first |

Fixed and documented in plan.md / git history: string escapes, `void'(f())`,
`#0;`, methods named `sample()`/`randomize()`, `'{}`, `const` property with
`new(...)`, `X::Y::method()`, `interface class`, `super.new` after a statement.

## Known gaps

- `const` class-property initializer widened to `expression`: full UVM compile
  went ~14min/1.3GB → ~17min/6.7GB RSS (not profiled). Narrow if it matters.
- ANTLR parse cost on large class bodies is high per line — §6.5 territory.
- `pickBestSymbol` tie within the same kind tier falls back to `path, line`.
- Symbols from an attached library DB are visible session-wide, not per
  project. No cycle detection for `libraryDbSources` chains.
- `handleExit` ignores clean vs. abnormal exit; `m_parentProcessId` unused.
- Macros not recorded as symbols (§6.25/§6.29).
- Covergroup methods and `randomize() with {…}` constraint completion
  unsupported.
