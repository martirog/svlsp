# svlsp — Handoff Document

**Last updated:** 2026-09-03 (compressed from full session history — see git log for
narrative detail if ever needed; this file now documents current-state-and-next-steps
only).

**Status:** Phases 3, 4, 5, 6.1, 6.2, 6.3 complete, plus §6.8 (debounced `didChange`
compilation) implemented 2026-08-28. On 2026-09-01: a `pickBestSymbol()`
hover/definition disambiguation improvement (see "Not yet done" #7), broadened
UVM-corpus completion coverage (#8), and §6.10 dot/member-access completion (#12,
single-segment first cut) all implemented, each with unit + functional test
coverage. On 2026-09-02: §6.9 keyword completion implemented, extended beyond
its original sketch with context-legality rules (a keyword is only offered
where its enclosing declaration scope makes it legal — e.g. `module` is
rejected once already inside any scope, since SV design units can't nest) —
see "Dot/member-access completion" sibling section below, "Keyword
completion" for the full design. Also on 2026-09-02: §6.13 built-in
container/type method completion implemented, expanded well beyond its
original scope (queues/associative arrays/mailbox/randomize) after the user
flagged it as under-researched — the shipped set is queues, associative
arrays, dynamic arrays, fixed-size unpacked arrays, `mailbox`, `semaphore`,
`process`, `string`, `event` (`.triggered`), plus randomize-family methods
on every class — see "Built-in container/type method completion" below.
Also on 2026-09-02: §6.14 chained/function-call dot-completion implemented
— `foo.bar.baz`, `func_ret_class().member`, and `this`/`super` chains now
resolve, subsuming §6.10's own deferred chained-access question. Found and
dropped an unnecessary piece of its own original design in the process
(a hand-maintained built-in-type-keyword list) — see "Chained/function-call
dot-completion" below. On 2026-09-03: §6.15 queue/associative-array element access completion
implemented — indexing into a container (`list[a].member`) to complete on
a class-typed *element*, including multi-dimensional partial-vs-full
indexing (`arr[i].` on a fixed-array-of-queues lands on the queue's own
methods; `arr[i][j].` reaches the element class) — see "Queue/
associative-array element access completion" below. Also on 2026-09-03,
following a real user bug report (`all_queue[i].get_policy` offered no
completions): §6.16 function/task prototype recording implemented — `pure
virtual`/`extern`/interface-class methods and DPI imports are now recorded
as symbols (previously invisible to every symbol-based feature entirely,
since only body-form functions/tasks had listeners), plus a companion
grammar fix for `interface_class_declaration` (defined but never wired
into any reachable parent rule — dead grammar, found while narrowing the
repro) — see "Function/task prototype recording" below. A related, real,
*separate* bug was found in the same investigation but deliberately not
fixed here (out of scope for §6.16): dot-completion can never resolve into
a class declared inside a package — see "Known gaps" below, candidate
`plan.md §6.17`. Unit suite now at 1445 assertions / 509 test cases, all
green (debug); Emacs integration suite gained
`test_28_keyword_completion.sh` (8 cases),
`test_29_builtin_method_completion.sh` (9 cases),
`test_30_chained_dot_completion.sh` (5 cases),
`test_31_queue_element_completion.sh` (7 cases),
`test_32_live_edit_completion.sh` (3 cases), and
`test_33_prototype_methods.sh` (3 cases), no regressions anywhere
(192/192 total). Working tree has uncommitted changes for §6.16 as of this
writing (everything through §6.15 is committed) — not yet committed, only
commit when asked.

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
tests/unit/        Catch2 unit tests (509 cases, 1445 assertions)
tests/integration/ Emacs functional test scripts (192 test cases across 34 files)
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

# From-scratch build (wipes build/<preset> first, then configure + build) —
# tools/build.sh [debug|release] [--target NAME], defaults to debug/everything
tools/build.sh                        # from-scratch debug build, everything
tools/build.sh release                # from-scratch release build, everything
tools/build.sh debug --target svlsp   # from-scratch debug build, svlsp only
tools/build.sh --help                 # full usage

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
`CompletionProvider::getCompletion` delegates to a shared private
`buildCompletionItems(candidates, prefix)` helper operating on a source-agnostic
`Candidate{name, kind, detail}` struct (added 2026-09-02 for keyword completion below —
previously took `vector<SymbolRow>` directly). The dot-completion branch (§6.10/§6.13/§6.14
below) builds its own `Candidate`s via `candidatesForResolvedType` rather than a
`vector<SymbolRow>`-taking overload — there's only ever one candidate-building path now,
regardless of chain length (see "Chained/function-call dot-completion" below).

**Keyword completion** (plan.md §6.9, implemented 2026-09-02, with context-legality
rules beyond the section's original sketch): `src/lsp/sv_keywords.h` holds a
~254-entry `SV_KEYWORDS` table (every identifier-syntax literal token in
`grammar/Sv.g4`, script-extracted), each tagged with a `KeywordContext` bitmask over
the 8 scope kinds `SymbolDatabase::scopeAtPosition` already tracks (`Module`/
`Interface`/`Program`/`Package`/`Class`/`Function`/`Task`, or `""` top level). A new
sibling, `SymbolDatabase::scopeKindAtPosition` (`src/db/symbol_database.h/.cpp`, same
query shape as `scopeAtPosition` but selects `kind` instead of the scope-name
expression), resolves the cursor's own scope kind. `getCompletion`'s non-dot branch
merges keywords whose mask matches that bit into the same `Candidate` pool as DB rows
*before* the emptiness check — an empty-DB file (e.g. a brand-new buffer) now
legitimately offers top-level keywords instead of returning null.

**Disclosed scope limit** (see plan.md §6.9 for the full writeup): legality is
enforced at *declaration-scope* granularity only, not statement/block granularity —
nothing here tracks "inside an `always_ff` block" vs "directly in the module body"
(both report scope kind `Module`, since `always`/`begin`/`if` aren't
`ParseRecordKind`s). This exactly covers the motivating example (SV design units
can't nest — `module` is correctly rejected once already inside any other scope) but
not finer per-statement precision. **A subtlety that bit this feature's own tests
during implementation, worth remembering for anyone extending the table:** an "end*"
keyword (`endmodule`, `endclass`, `endfunction`, `endtask`, ...) is typed while still
*inside* the body it closes, so it must be scoped to that body's own kind (`KwModule`,
`KwClass`, ...) — not wherever its matching opener is itself legal to type. Getting
this backwards was an actual bug caught by `tests/unit/lsp/test_completion.cpp`'s new
context-legality cases before it shipped.

**Dot/member-access completion** (plan.md §6.10, implemented 2026-09-01;
chained access implemented 2026-09-02, see "Chained/function-call
dot-completion" below): `dotCompletionContext(text, line, character)`
(`src/lsp/symbol_utils.h/.cpp`) detects a chain of identifiers/calls
immediately preceded by a `.` (e.g. `foo.b|` → one segment `{"foo",
isCall:false}`, prefix `"b"`), sharing `wordAtPosition`'s left/right
identifier-walk logic via a private `findLineBounds` helper. Originally
(2026-09-01) only the single segment before the *last* `.` was captured;
generalized 2026-09-02 to an arbitrary-length `vector<ChainSegment>` — a
1-segment chain reproduces the original behavior exactly, so this was a
strict superset, not a new mechanism (see below).

When `getCompletion` sees a dot-completion context, it resolves the chain
left to right (`resolveChain`/`resolveFirstSegment`/`resolveMemberSegment`
in `src/lsp/completion.cpp`) to whatever type/scope backs the final
segment's members, then builds candidates for that via
`candidatesForResolvedType`. Any failure to resolve at any hop (undeclared
object, built-in type, wrong kind, unknown scope) returns `nullptr` rather
than the whole visible scope — offering unrelated symbols after an
explicit `.` would be worse than offering nothing.

The declared type itself comes from a new `userTypeName()` helper
(`src/compiler/sv_tree_walker.cpp`) that reads a `data_type_or_implicit`'s
`type_identifier()`/`class_type()` accessor (both alternatives an unqualified
class/interface/typedef name can parse as — see the `data_type` grammar-ambiguity
row in "Sv.g4 grammar quirks" below) and returns `""` for every other alternative
(built-in types, struct/enum literals, `string`, `event`, ...). Wired into
`enterData_declaration` and both `enterParameter_declaration`/
`enterLocal_parameter_declaration` (→ `extractParams`'s new optional `typeName`
param), populating `ParseRecord::detail` for `Signal`/`Parameter` only.
**Deliberately not wired into `Port`**: `Port`'s `detail` already holds direction
(`"input"`/`"output"`/...) and `test_sv_listener.cpp`/`test_document_symbols.cpp`
hardcode that — overloading it with type text would silently break existing Port
hover. Dot-completion on a class/interface-typed port isn't supported by this first
cut.

Also required a fix in `enterNet_declaration`, not just `enterData_declaration`: a
bare `MyClass foo;` turned out to be grammatically ambiguous between
`data_declaration` (`MyClass` classified as `data_type`'s `type_identifier`) and
`net_declaration`'s own alt 2 (`MyClass` classified as a `net_type_identifier` — a
user-defined nettype) — this grammar resolves that case via the latter, so
`enterData_declaration` alone never saw it. A second, distinct manifestation of the
same "identifier classification needs a symbol table" problem already documented
for `data_type`'s own internal alternatives (see grammar quirks table) — not
previously noticed because nothing depended on which alt fired until now.

**Built-in container/type method completion** (plan.md §6.13, implemented
2026-09-02): extends the dot-completion path above for everything with
*implicit, language-defined* methods but no `Class`-kind `ParseRecord` of
its own — queues, associative arrays, dynamic arrays, fixed-size unpacked
arrays, `mailbox`, `semaphore`, `process`, `string`, `event`
(`.triggered`), plus the randomize-family methods every class implicitly
gains. Originally scoped narrower (queues/associative arrays/mailbox/
randomize only); expanded after the user rejected a first-pass plan as
under-researched, naming `process` as a concrete miss.

Detection is two mechanisms, both new in `src/compiler/sv_tree_walker.cpp`'s
`enterData_declaration`, layered by priority per declarator (container
dimension wins over bare-type tag wins over `userTypeName()`'s ordinary
class/interface name — e.g. `string s[$]` tags as a queue, not a string,
since the container's own methods are what matter):
1. `containerDimensionTag(vda)` scans a declarator's own
   `variable_dimension()` list (not the shared `data_type_or_implicit` —
   `int a[$], b;` must tag only `a`) for the first
   `queue_dimension()`/`associative_dimension()`/`unsized_dimension()`/
   `unpacked_dimension()` hit, in declaration order (outermost/leftmost
   wins for a multi-dimensional declarator like `int q[4][$]`).
2. `builtinBareTypeTag(dtoi)` catches `string`/`event`, which
   `userTypeName()` structurally cannot see — both are bare literal
   `data_type` alternatives (`grammar/Sv.g4:746-751`) with no
   `type_identifier()`/`class_type()` accessor, detected instead via
   `data_type()->getText() == "string"/"event"` (reliable here specifically
   because those two alternatives have exactly one terminal child and no
   `packed_dimension`).

Both tag with new `$`-prefixed sentinel constants in
`src/compiler/parse_record.h` (`CONTAINER_QUEUE`/`CONTAINER_ASSOC`/
`CONTAINER_DYNAMIC_ARRAY`/`CONTAINER_FIXED_ARRAY`/`CONTAINER_STRING`/
`CONTAINER_EVENT` — SV identifiers can never start with `$`, so these can
never collide with a real type name). `mailbox`/`semaphore`/`process` need
no tag at all: none are grammar keywords, so all three already parse as
ordinary parameterized `class_type` references and `userTypeName()`
already extracts them verbatim — confirmed live via `hover` before writing
any code, not assumed from reading the grammar (see the empirical-
verification note below).

`src/lsp/sv_builtin_methods.h` holds the static method tables
(`QUEUE_METHODS`, `ASSOC_ARRAY_METHODS`, `DYNAMIC_ARRAY_METHODS`,
`FIXED_ARRAY_METHODS`, `MAILBOX_METHODS`, `SEMAPHORE_METHODS`,
`PROCESS_METHODS`, `STRING_METHODS`, `EVENT_MEMBERS`, `RANDOMIZE_METHODS`)
and one dispatcher, `builtinMethodsFor(detail)`, checking the five tags
plus the three literal type names. `CompletionProvider::getCompletion`'s
dot-completion branch (`src/lsp/completion.cpp`) tries this dispatcher
first (no DB call at all for a built-in hit); otherwise it falls through to
the unchanged `findSymbolsInScope(detail)` call, plus a *separate*
`findSymbolsByName(detail)` scan gating whether to union
`RANDOMIZE_METHODS` in — gated on an actual `Class`-kind row existing (not
just "did `findSymbolsInScope` return anything"), so an unresolved/bogus
type keeps failing closed instead of surfacing `randomize()` for a made-up
name, and a class that overrides `randomize` itself keeps the real DB
entry rather than getting a duplicate synthetic one (checked by name
collision against the already-fetched member rows).

**Empirical verification, not grammar-reading, drove every detection
claim above**: this codebase already has one documented case
(`data_type`/`net_declaration` ambiguity, "Sv.g4 grammar quirks" below)
where reading the grammar text does not reliably predict which
alternative ANTLR's default prediction actually fires. Before finalizing
this design, every claim (dynamic arrays already recorded; `process`/
`semaphore` already resolve via the ordinary `class_type` path; `string`/
`event` genuinely need a new detection path) was checked by piping real
`initialize`/`didOpen`/`documentSymbol`/`hover` requests into
`build/debug/svlsp` directly (the smoke-test pattern under "Build and
test" above) rather than assumed from the grammar alone — this caught a
wrong first-pass assumption (that dynamic arrays weren't recorded as
symbols at all) before it shipped as a false claim.

**Disclosed uncertainty, unchanged from plan.md §6.13's original text:**
every method name/signature in the tables above is reconstructed from
training-time familiarity with IEEE 1800-2017, not verified against an LRM
copy (this server has no access to one).

**Deliberately still out of scope:** covergroup built-in methods
(`cg.sample()`, `cg.get_coverage()`, ...) — confirmed covergroups aren't
tracked as any `ParseRecordKind` today (no `enterCovergroup_declaration`
exists), so supporting this needs a new `ParseRecordKind` and tree-walker
listener first — disproportionate to this pass, left as a follow-up.

**Chained/function-call dot-completion** (plan.md §6.14, implemented
2026-09-02): resolves `foo.bar.baz`, `func_ret_class().member`, and
`this`/`super` chains — the last gap in the dot-completion family, and
§6.10's own deliberately-deferred chained-access question.
`dotCompletionContext`'s new `segments: vector<ChainSegment>` (see
"Dot/member-access completion" above) is built by walking left one
segment at a time; a segment ending in `)` is read as a call via
paren-balance matching back to its `(` with a minimal in-string state (so
`foo("a.b").c` isn't confused by the `.` inside the string literal, and
`a(b(c)).d` matches the outer call, not the inner one) — verified by hand
trace against both of those exact cases while designing this, the same
"don't trust reading the parser alone" discipline §6.13 established for
the ANTLR grammar, applied here to this hand-rolled one.

Resolution (`src/lsp/completion.cpp`) is iterative left to right:
`resolveFirstSegment` resolves segment 0 against what's visible at the
cursor (`this`/`super` via a new `SymbolDatabase::enclosingClassNameAt`
— finds the nearest enclosing `Class` even when the cursor is nested
inside one of its *methods*, which `scopeKindAtPosition` alone can't do
since the innermost scope kind there is `Function`; a call via
`Function`-kind `findSymbolsVisibleAt` lookup; a bare identifier via
`Signal`/`Parameter`, §6.10's original behavior unchanged); each later
segment resolves the same way but as a *member* of the previous segment's
resolved class, via `findSymbolsInScope`. `super` reads the enclosing
class's own parent-class `detail` (`findSymbolsByName` + `pickBestSymbol`
— the same cross-file disambiguation hover/definition already use), not
the class itself — a dedicated test has a child class *override* the
exact method being chained through, proving `super` resolves through the
parent's version, not the override.

**A simplification found while implementing, replacing this section's own
original design:** a hand-maintained built-in-type-keyword list (meant to
tell a `Function`'s raw, unfiltered return-type text like `"void"`/`"int
unsigned"` apart from a genuine class name) turned out to be unnecessary.
SV reserved words can never be valid identifiers, so that raw text can
*structurally never* collide with a real class/scope name — every DB
lookup on it already comes back empty on its own, with no false-positive
risk. So every hop just feeds whatever it resolved to straight into the
next lookup uniformly, whether that came from a `userTypeName()`-filtered
Signal/Parameter `detail` or a Function's raw return-type text — no
keyword list, no pre-check, no schema change. This did surface one real
landmine the keyword-list approach would have sidestepped by accident:
`""` is not "unknown type" in this schema, it's the literal *top-level
scope* value every top-level symbol is stored under, so
`findSymbolsInScope("")` returns the whole project's top-level symbols
rather than nothing — every hop (not just the first, as §6.10 alone
required) now explicitly guards against an empty resolved type, via one
shared `candidatesForResolvedType(db, detail)` (also where §6.13's
`builtinMethodsFor` dispatch and the `RANDOMIZE_METHODS` union now live —
extracted from the old single-hop-only code, called once regardless of
chain length, so a 1-segment chain reproduces §6.10/§6.13's original
behavior exactly rather than through a separate path).

**Queue/associative-array element access completion** (plan.md §6.15,
implemented 2026-09-03): extends §6.13/§6.14 to *indexing into* a
container (`list[a].member`) to complete on a class-typed element, not
just the container's own methods — including multi-dimensional
declarators like `MyClass arr[4][$]` (a fixed array of queues), where
partial indexing (`arr[i].`) must land on the queue's own methods and
full indexing (`arr[i][j].`) must land on `MyClass`'s own members.

`containerDimensionTag()` (§6.13, singular — first dimension only) became
`containerDimensionTags()` (plural, `src/compiler/sv_tree_walker.cpp`):
walks every `variable_dimension()` entry on a declarator, not just the
first. `enterData_declaration` now joins those tags with the element
type/tag (`userTypeName()`/`builtinBareTypeTag()`) into one `:`-delimited
`ParseRecord::detail` string, outermost layer first — e.g.
`"$fixed_array:$queue:MyClass"` for `MyClass arr[4][$]`, or just `"$queue"`
for `int q[$]` (no element layer: a built-in scalar has no tag). No schema
change; `SV_KEYWORDS`-style reserved-word reasoning doesn't apply here, but
the same "no collision risk" logic does: `:` is safe because every layer
is either a `$`-prefixed tag or a bare identifier, never itself containing
`:`.

`ChainSegment` (`src/lsp/symbol_utils.h`) gained an `indexDepth` field
(default 0, meaning "not indexed" — every pre-§6.15 segment).
`dotCompletionContext` detects a segment ending in `]` and
bracket-balance-matches left via a new `matchingOpenDelim` helper — a
generalization of §6.14's `matchingOpenParen` to an arbitrary open/close
delimiter pair, now shared by both — repeating for each further
consecutive `]` immediately to its left so `arr[i][j]` is parsed as *one*
segment with `indexDepth=2`, not two chain segments (there's no `.`
between the brackets). Same in-string tracking as §6.14's call-paren
matching, so an index key like `aa["a.b"]` isn't confused by the `.`
inside the string.

Resolution (`src/lsp/completion.cpp`) adds
`peelDimensionLayers(detail, depth)`: splits `detail` on `:`, counts the
*leading run* of recognized container-dimension tags
(`isContainerDimensionTag` — only `$queue`/`$assoc_array`/
`$dynamic_array`/`$fixed_array` count; `$string`/`$event`/a bare class
name are element layers, never themselves indexable), fails closed (`""`)
if `depth` exceeds that count — over-indexing, or indexing a detail with
no container dimensions at all — otherwise returns the remaining layers
rejoined. `resolveFirstSegment`/`resolveMemberSegment` both pipe their
resolved row's `detail` through this before returning it, so a
1-segment-deep, `indexDepth=0` chain reproduces §6.10/§6.13/§6.14's
original behavior exactly — this is a strict superset, not a new
mechanism, matching every earlier phase's own precedent in this feature
area.

**One addition beyond the original plan.md sketch, found while
implementing:** `candidatesForResolvedType`'s builtin-method dispatch
(`builtinMethodsFor`) now keys off a new `firstLayer(detail)` helper (the
substring before the first `:`) instead of the full `detail` string. This
turned out to be *required*, not optional: once `detail` could carry
trailing element-type layers, even *unindexed* container access broke —
`"q."` on `MyClass q[$]` now has `detail="$queue:MyClass"` rather than
the old bare `"$queue"`, so exact-string dispatch would have silently
regressed every existing §6.13 test. Caught by the unit suite before
shipping, not by inspection. A useful side effect of the same fix: partial
indexing that lands back on a container (not yet the element) correctly
offers that container's own methods through this same path, and — not
separately tested, but falls out for free from the same general
mechanism — a queue of `string`/`event`/`mailbox`-family elements now
also offers *those* types' own methods one level of indexing in.

Explicitly still out of scope, per plan.md §6.15's own original text:
associative-array key-type validation (any index expression is accepted,
never checked against the declared index type); indexing directly off a
*call's* result (`get_matrix()[i][j].member`); struct-typed elements
(classes only, matching §6.14's own restriction).

**Function/task prototype recording** (plan.md §6.16, implemented
2026-09-03): a real user bug report — `all_queue[i].get_policy` offered no
completions, where `get_policy` was declared `pure virtual function
policy_base get_policy(uvm_object par);` — traced to
`src/compiler/sv_tree_walker.cpp` never recording a symbol for *any*
function/task declaration without a body. Only
`enterFunction_body_declaration`/`enterTask_body_declaration` existed,
both requiring `function_data_type_or_implicit ... 'endfunction'`/`...
'endtask'`; the grammar's separate, body-less `function_prototype`/
`task_prototype` rules (`grammar/Sv.g4`) had no listener at all — silently
breaking hover/definition/completion/references for any `pure virtual`
method, `extern`-declared method, or interface-class method (interface
classes are *always* prototype-only). Confirmed with a clean,
zero-diagnostic minimal repro before touching any code (this project's own
"empirical verification, not grammar-reading" discipline, per §6.13) —
17 files in a real UVM checkout use `pure virtual function`/`task`.

Fixed with one new listener pair,
`enterFunction_prototype`/`enterTask_prototype`
(`src/compiler/sv_tree_walker.cpp`), covering `pure virtual`, `extern`,
interface-class-method, *and* DPI-import shapes uniformly — no per-context
dispatch needed, since ANTLR fires one callback per *grammar rule*
regardless of which parent alternative reached it, and all four shapes
reduce to the same `function_prototype`/`task_prototype` rules (confirmed
via the generated `SvParser.h`'s exact accessor shapes before writing any
listener code). A DPI-imported function picking up a symbol too is a
harmless, arguably-correct bonus, not scope creep. **The one non-obvious
pitfall, found by reading `pushId()` closely:** it unconditionally pushes a
new scope frame for `Function`/`Task` kinds, on the premise a body will
eventually pop it via the existing body-form exit listeners. A prototype
has no body — without a matching `exitFunction_prototype`/
`exitTask_prototype` popping that frame immediately, every symbol declared
afterward in the file would be silently nested under the leaked scope
(structurally the same corruption class the user's own unrelated
`` `import ``-typo bug independently demonstrated is possible here). A
dedicated regression test (a pure-virtual method followed by an ordinary
declaration, asserting the second one's `scope`/`parent` didn't leak)
guards this specifically.

A **second, related bug** was found while narrowing the repro:
`interface_class_declaration` (`grammar/Sv.g4:120`, `// ROOT node`
comment) was defined but never referenced from any reachable parent rule
— completely dead grammar. Every real-world use (confirmed against the
user's own project) produced spurious "extraneous input 'interface'..."
parse errors; ANTLR's error recovery discarded the `interface` token and
happened to reparse the remainder as an ordinary `class_declaration`,
which is why it silently "mostly worked" (misclassified as a plain Class,
alongside false-positive diagnostics) rather than failing loudly. Fixed by
adding `interface_class_declaration` to `package_or_generate_item_declaration`
— the reported failure shape (an interface class declared inside a
`package`). Its own methods (`interface_class_method`) reduce to the exact
same `method_prototype` rule pure-virtual/extern class methods already
use, so no separate listener was needed for them either — one fix
serves both. Interface classes are recorded via
`enterInterface_class_declaration`, reusing `ParseRecordKind::Class` (no
schema change, no dispatch changes anywhere else); their `detail` records
only the *first* listed parent when `extends`ing multiple other interface
classes, matching the single-inheritance assumption `super.` resolution
already makes for ordinary classes project-wide (a disclosed, tested
limitation, not an oversight).

**No LSP-layer code changed at all** — because both new record kinds
reuse existing `ParseRecordKind::Function`/`Task`/`Class`, every consumer
(`hover.cpp`, `definition.cpp`, `document_symbols.cpp`, `completion.cpp`
including every dot-completion path above) already handles them uniformly
regardless of body-vs-prototype. Purely additive in the compiler
front-end. Verified end-to-end against the real, motivating project files
(zero diagnostics, both previously-missing methods now recorded) — see
"Known gaps" below for a *separate*, pre-existing bug found in the same
investigation (dot-completion can't resolve into a package-nested class's
members at all) that would still block that exact real-world case, not
fixed here.

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
| A bare `MyClass foo;` at `module_common_item` level is ALSO ambiguous between `data_declaration` (`MyClass` as a `data_type`) and `net_declaration` alt 2 (`MyClass` as a `net_type_identifier`, i.e. a user-defined nettype) — a second, distinct manifestation of the same underlying problem, discovered 2026-09-01 building §6.10 dot-completion's type-detail population (`enterData_declaration` alone never saw plain class-typed signals; this grammar resolves them via `enterNet_declaration` instead) | **Not a diagnostic-producing bug** — both alts still record a `Signal` with the right name, so hover/definition/completion were unaffected before 2026-09-01. Only became visible because `userTypeName()` needed wiring into the alt that actually fires. Now handled in both listener methods (see "Dot/member-access completion" above). | Resolved for the one dependent (§6.10); the underlying grammar ambiguity itself is unchanged |
| `interface class Foo; ... endclass` (LRM's true interface-class construct — always prototype-only methods, common in UVM-style code) | **Fixed 2026-09-03 (§6.16).** `interface_class_declaration` was defined in the grammar (`// ROOT node` comment) but never referenced from any reachable parent rule — completely dead grammar. Every use produced spurious "extraneous input 'interface'..." parse errors; ANTLR's error recovery discarded the `interface` token and happened to reparse the remainder as an ordinary `class_declaration`, which is why it silently "mostly worked" (misclassified, alongside false-positive diagnostics) rather than failing loudly. Found while investigating a real user bug report, confirmed against a real project file before fixing. | **Fixed** — added `| interface_class_declaration` to `package_or_generate_item_declaration` (the reported failure shape: an interface class declared inside a `package`). Deliberately not wired into `module_or_generate_item`/top-level `description` — no demonstrated failing case for those placements; revisit if one surfaces. |

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
- **Dot-completion can never resolve into a class declared inside a package**
  (found 2026-09-03 while building §6.16's functional test — a real, separate,
  pre-existing gap, not part of §6.16 itself). `findSymbolsInScope(scope)`
  (`src/db/symbol_database.cpp`) does an exact `s.scope = ?` match, but a
  class-typed `Signal`/`Parameter`'s own `detail` comes from `userTypeName()`
  (`src/compiler/sv_tree_walker.cpp`), which "already never produces a
  qualified/`::`-containing name" — a deliberate simplification going back to
  §6.10. For a class declared inside a `package` (its members' own `scope`
  column is the fully-qualified chain, e.g. `"policy_pkg::PolicyImpl"`), that
  bare, unqualified `detail` (`"PolicyImpl"`) can never match, so
  `candidatesForResolvedType` always falls through to the empty-scope,
  randomize-union-only path — real class members are silently invisible to
  `.` completion. Confirmed directly against a real UVM-style file (a class
  extending `uvm_object`, declared inside a package): dot-completion offered
  only the synthetic `randomize`-family methods, never the class's own real
  members. Every dot-completion fixture in this repo happens to declare its
  classes at top level (where the bare name and the scope chain coincide),
  which is why this was never caught before — real-world verification code
  (essentially all real UVM classes) almost always lives inside a package.
  Not fixed here — likely needs `userTypeName()`/`findSymbolsInScope` (or a
  new qualified-lookup variant) to carry/match the full scope chain instead
  of a bare name; a real, standalone piece of work, candidate for `plan.md
  §6.17`.
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
11. ~~Keyword completion (`plan.md §6.9`)~~ — **implemented 2026-09-02**,
    extended beyond the original sketch with context-legality rules (a
    keyword is only offered where its enclosing declaration scope makes it
    legal, per the user's explicit request — e.g. `module` rejected once
    already inside any scope, since SV design units can't nest). See
    "Keyword completion" under "LSP feature providers" above for the full
    design and its disclosed declaration-scope-granularity limit, and
    plan.md §6.9 for the complete writeup (superseding its own original
    "not pursuing syntactic position-awareness" sketch, kept there for
    history).
12. ~~Dot / member-access completion (`plan.md §6.10`)~~ — **implemented
    2026-09-01**, single-segment (`foo.bar`) first cut as recommended;
    chained access (`foo.bar.baz`) and `this`/`super` still deferred. See
    "Dot/member-access completion" under "LSP feature providers" below for
    the full design, and plan.md §6.10 for the two scoped deviations from
    its original sketch (Port excluded from `detail`-population; a real
    `data_declaration`/`net_declaration` grammar ambiguity surfaced along
    the way).
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
16. ~~Built-in container & type method completion (`plan.md §6.13`)~~ —
    **implemented 2026-09-02**, expanded from its original queue/
    associative-array/mailbox/randomize scope after the user flagged it as
    under-researched: also covers dynamic arrays, fixed-size unpacked
    arrays, `semaphore`, `process`, `string`, and `event` (`.triggered`).
    See "Built-in container/type method completion" under "LSP feature
    providers" above for the full design (including the empirical,
    live-`svlsp`-probe verification method used to catch a wrong first-pass
    assumption before it shipped) and plan.md §6.13 for the complete
    writeup. Remaining open item, deliberately deferred: covergroup
    built-in methods (`cg.sample()`, ...) — would need a new
    `ParseRecordKind`, disproportionate to this pass. `randomize() with
    {...}` constraint-block completion also remains out of scope (a
    distinct, harder grammar construct — completion *inside* the `with`
    block itself, not the bare `randomize` method name).
17. ~~Function-call return-type chained dot-completion (`plan.md §6.14`)~~
    — **implemented 2026-09-02**. `foo.bar.baz`, `func_ret_class().member`,
    multi-level chains, and `this`/`super` chains all resolve now,
    subsuming §6.10's own deferred "chained access" question. See
    "Chained/function-call dot-completion" under "LSP feature providers"
    above for the full design and plan.md §6.14 for the complete writeup —
    notably, a simplification found during implementation that dropped an
    entire piece of the original design (a hand-maintained built-in-type-
    keyword list, replaced by relying on SV's own reserved-word rules).
    Remaining out of scope, per the original sketch, unchanged: struct
    member access chained off a function call (LRM permits it, real
    simulators don't); `Class::static_method()` call syntax as a chain's
    first segment; constraint-block completion inside `randomize() with
    {...}`; no depth cap on chain length (not stress-tested against a
    pathological input).
18. ~~Queue/associative-array element access completion, including
    multi-dimensional (`plan.md §6.15`)~~ — **implemented 2026-09-03**,
    following the sketch essentially as designed. See "Queue/
    associative-array element access completion" under "LSP feature
    providers" above for the full design (including one real addition
    found while implementing — `firstLayer()`-based builtin-method
    dispatch, required to keep unindexed container access from silently
    regressing once `detail` could carry element-type layers) and
    plan.md §6.15 for the complete writeup. Remaining out of scope, per
    the original sketch, unchanged: associative-array key-type
    validation; indexing directly off a *call's* result
    (`get_matrix()[i][j].member`); struct-typed elements (classes only).
19. ~~Record function/task prototypes as symbols — pure virtual, extern,
    interface-class methods, DPI import (`plan.md §6.16`)~~ —
    **implemented 2026-09-03**, following a real user bug report
    (`all_queue[i].get_policy` offered no completions; `get_policy` was
    `pure virtual`, never recorded as a symbol at all since only
    `enterFunction_body_declaration`/`enterTask_body_declaration` existed,
    both requiring a body). Two new listener pairs in
    `src/compiler/sv_tree_walker.cpp` — `enterFunction_prototype`/
    `enterTask_prototype` (covering pure-virtual, `extern`,
    interface-class-method, and DPI-import shapes uniformly, since all
    four reduce to the same two grammar rules) and
    `enterInterface_class_declaration` (reusing `ParseRecordKind::Class`,
    no schema change) — plus a companion grammar fix: `grammar/Sv.g4`'s
    `interface_class_declaration` rule was defined but never referenced
    from any reachable parent rule (dead grammar, found while narrowing
    the repro; see "Sv.g4 grammar quirks" above), now wired into
    `package_or_generate_item_declaration`. See plan.md §6.16 for the full
    writeup, including a documented pitfall found while implementing:
    `pushId()` unconditionally pushes a scope frame for `Function`/`Task`
    kinds on the assumption a body will eventually pop it — a prototype
    has no body, so its own `exit...` listener must pop that frame
    immediately or every symbol declared afterward in the file would be
    wrongly nested under it (a regression test guards this specifically).
    No LSP-layer code changed at all — every consumer already handles
    `Function`/`Task`/`Class` uniformly regardless of body-vs-prototype.
20. **Dot-completion into a package-nested class's members
    (`plan.md §6.17`, candidate)** — not started, found 2026-09-03 while
    building §6.16's functional test (see "Known gaps" above for the full
    writeup). `findSymbolsInScope`'s exact-match `scope` lookup can never
    match `userTypeName()`'s deliberately-unqualified `detail`, so `.`
    completion on any class declared inside a `package` — i.e. essentially
    every real UVM/verification class — silently falls through to
    offering only the synthetic randomize-family methods, never the
    class's real members. A real, standalone gap, not part of §6.16;
    every existing dot-completion fixture happens to use top-level classes,
    which is why it was never caught before.
