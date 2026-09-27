# svlsp — Handoff Document

**Last updated:** 2026-09-27 (comma-shorthand parameters fixed; §6.30 follow-ups in progress, constructors first).

This file covers only **current state, what's next, and what you need to know
to work in the repo**. The full design and implementation history of every
finished section lives in `plan.md` (each §6.x has its own **Status:** line and
write-up) — read the relevant section there rather than looking for it here.

---

## Current state

**PICK UP HERE NEXT TIME — §6.30 follow-ups (item 0 of "Other open
work"), starting with recording constructors.** §6.29, §6.30 and the
comma-shorthand parameter fix are complete.

- Committed on `main`: everything through §6.30 and §6.29, including
  macro hover/definition/references/rename, and the comma-shorthand
  parameter fix (`function f(input int a, b)` now records `b` as
  `input int`; LRM 13.3 inheritance in `enterTf_port_item`).
- Test baselines:
  - unit: **848 cases**, all passing (no `[!shouldfail]` known gaps left
    -- the `` `define `` references case passes since the macro follow-up)
  - Emacs functional: **245/245**
  - UVM corpus (opt-in): **1031 assertions / 22 cases**, all passing.
    (499 at §6.30 step C → 388 at step D: the empty-prefix completion
    case checks each item and step D's (name, kind) dedup removed 124
    duplicates; +10 for §6.29 part A; +633 for the macro follow-up, whose
    references case checks each of its 313 locations.)
  - UVM corpus `--build-db`: **1 diagnostic, 15327 symbol rows, 553
    macro rows, 163 files** (~17 min, ~7 GB RSS; don't run it at the same
    time as the corpus tests, and a laptop suspend pauses either; when
    waiting on one from a script, don't `pgrep -f` its command line --
    the waiting shell's own command line matches)

**What §6.30 changed that other work must know about:**
- Hover/definition/references/rename all go through
  `src/lsp/symbol_resolution.h` (`resolveSymbolAt`, `resolveSymbolsAt`,
  `overrideFamilyId`). References and rename share
  `ReferencesProvider::findOccurrences`.
- `symbols.detail` for Signal/Parameter/Class keeps type qualifiers:
  `pkg_b::Item`, `base_p::Base`, `p::Outer::Inner`, `$unit::T`
  (`#(...)` dropped). Split layered details with `splitLayers`/
  `firstTypeLayer` (single `:` only), never a naive `:` split.
- `baseClassChain` accepts qualified names.
- A block's leading assignment (`x = 1;`) is no longer recorded as a local
  Signal declaration (`isMisparsedAssignment`) — 2514 phantom rows gone
  from UVM.
- New symbol kinds (step C): `Typedef`, `EnumLiteral`, `Member` (struct/
  union field, scoped `<chain>::<variable-or-typedef>` — never on a lexical
  chain), `Genvar`. Anything that switches on kind strings must consider
  them.
- Out-of-class method bodies (step D): `function void C::m()` is recorded
  in the class's scope (`p::C`, next to the extern prototype) and its body
  scope is `p::C::m`. So a prototype and its body share one Function name
  and one Port scope: collect a method's parameters with
  `SymbolDatabase::portsOf(scope)` (first declaration only), never a raw
  `findSymbolsInScope` + Port filter. `enclosingClassNameAt` and the
  resolver fall back to the scope chain to find the class of such a body.
- `fixtures/defref_top.sv` has appended sections of same-named decoys
  (`defref_pkg_b`, `defref_top_b`), step-C kinds (`defref_kinds`) and an
  out-of-class body (`defref_ooc_p`); append new cases after them.

## Next up — candidates

- **Advertise `triggerCharacters = {"(", ","}`** for signature help in
  `server_state.cpp` (plan.md §6.29 "Shared infrastructure"; every shape
  is `(`/`,`-driven). Not done yet.
- **Macros in workspace symbols** (small; `findMacros` exists, a
  prefix query over the `macros` table doesn't yet).
- Or the "Other open work" list below.

What §6.29 changed that other work must know about:
- Macros live in their own `macros` table (schema v10: name, file, line,
  column, parameters, body), never in `symbols`. Whether a position is a
  macro name is decided from the text alone by `src/lsp/
  macro_resolution.h` (`macroNameAt`, `isMacroOccurrence`); hover,
  definition, references/rename and signature help all check it first and
  never fall through to the symbol resolver. A non-macro symbol's
  references skip macro-shaped hits.
- `blankCommentsAndStrings` now lives in `symbol_utils`.
- Signature-help lookup order: macro (`` `name( ``, never falls through)
  → keyword table (`sv_keyword_signatures.h`) → instantiation → bare call
  (DB, then the `$` table in `sv_system_tasks.h`) → dotted call.
- A `define-only header now gets a `files` row and is listed in
  `includedFiles`/`file_includes`, so editing it recompiles includers.
- Emacs fixtures: `ref_rename_sighelp.sv` is slow to compile in the ASan
  build (~9 s) and near test_12's first-request timeout — put new
  signature-help cases in their own small fixture (as
  `fixtures/sighelp_*.sv` do), not appended there.

## Other open work (priority roughly top-down)

0. **§6.30 follow-ups** (none blocking):
   - constructors are never recorded (no `class_constructor_declaration`
     handler): `function C::new(...)` has no Function row and its
     arguments/locals land in the enclosing package's scope, visible
     package-wide (e.g. `error_str`, `top`, `cs` from
     `uvm_component::new` in `uvm_pkg`). In-class `new` isn't recorded
     either;
   - the resolver doesn't follow a Typedef to the class it aliases
     (`alias_t x; x.get()` resolves `get` by name only);
   - bare-name completion never offers inherited class members (dot-
     completion does), inside in-class and out-of-class bodies alike;
   - disclosed limits kept from the plan: `begin`/`end` and generate
     blocks aren't scopes, scope containment is line-granular, struct /
     interface / hierarchical receivers use the name-only fallback;
   - a class's own members are found from an out-of-class body in another
     file, but the visible set's local chain (and so completion) still only
     covers the cursor's file.
1. **Completion still uses `wordAtPosition`, which ignores comments/
   strings.** Hover/definition/references/rename are fixed (they use the
   resolver, which blanks comments and strings; unit-tested in
   `test_hover.cpp`). Completion typed inside a comment or string still
   offers symbols. Original report: hovering `put` in `endfunction // put`
   (`/home/martin/src/policy/policy_mixin.sv:37`) returned an unrelated
   `uvm_cache` method. No functional (Emacs) test for the comment case yet.
2. **§6.21** semantic reference-resolution diagnostics (unresolved type/import)
   — not started. Related real bug: a live edit's `replaceDiagnostics` wipes
   diagnostics `LibraryResolver` appended earlier (one undiscriminated
   `diagnostics` table).
3. **§6.24** audit compiler warnings from `tools/build.sh` — not started.
4. **§6.12** configurable debounce interval (`debounceMs`) — not started.
5. **§6.20** external read-only DB access — not started.
6. **§6.22 follow-up**: UVM-corpus coverage for rename (references has
   it since §6.30 step B, `tests/uvm_corpus/test_references_uvm.cpp`).
7. **§6.19** library-DB staleness detection (source changed since build) —
   open question, no shape chosen.
8. **§6.5** performance — open; see "Known gaps" for the concrete costs.
9. Minor: comment at `pathToUri()` explaining that `fromPath()` always
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
                   macro_resolution,
                   signature_help, document_symbols, workspace_symbols, symbol_utils,
                   sv_keywords.h, sv_builtin_methods.h, sv_system_tasks.h,
                   sv_keyword_signatures.h, project_manifest_parser,
                   project_registry, library_db_builder
src/compiler/      compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                   parse_record, parse_cache, filelist_parser, project_config, file_utils
src/db/            database, symbol_database, compilation_controller,
                   library_resolver, project_compiler, schema (v10)
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
| Database | SQLite3 amalgamation, schema v10 |
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
| Hover / Definition | `resolveSymbolAt` (`src/lsp/symbol_resolution.h`, plan.md §6.30): `::` qualifiers, named connections, `.` receiver chains, bare names innermost-scope-first; `exact=false` name-only fallback (`findSymbolsByName` + `pickBestSymbol`) when the qualifier/receiver isn't understood |
| Completion | `findSymbolsVisibleAt` + keywords (`sv_keywords.h`, scope-kind legality) → fuzzy scoring (toggle: `initializationOptions.svlsp.fuzzyCompletion`). Dot-completion: `dotCompletionContext` → `resolveChain` → `membersAcrossChain` (full `extends` walk), builtin container methods via `sv_builtin_methods.h` |
| References / Rename | `ReferencesProvider::findOccurrences`: lexical cross-file `findIdentifierOccurrences`, each hit resolved with `resolveSymbolsAt` and kept if it lands on the target row / same method override family (`overrideFamilyId`), or only resolves by fallback. Rename edits exactly that set |
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

### Database (`src/db/`, schema v10)

Tables: `files`, `symbols (kind, name, line, col, parent, detail, end_line,
scope)`, `diagnostics`, `imports (pkg_name, item, is_export)`,
`instantiations`, `library_include_dirs`, `library_build_info`,
`file_includes`, `macros`. Migrations run automatically in `database.cpp`.

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
- Macros live in their own `macros` table, not `symbols` (§6.29), so
  anything that only queries `symbols` (e.g. workspace symbols) misses them.
- Covergroup methods and `randomize() with {…}` constraint completion
  unsupported.
