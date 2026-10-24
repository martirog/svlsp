# svlsp — Handoff Document

**Last updated:** 2026-10-24 (C7, C5, C1 committed; C2 is next).

This file covers only **current state, what's next, and what you need to know
to work in the repo**. The full design and implementation history of every
finished section lives in `plan.md` (each §6.x has its own **Status:** line and
write-up) — read the relevant section there rather than looking for it here.

---

## Current state

**PICK UP HERE NEXT TIME -- §6.33 C2: make the grammar's hot
decisions cheap** (the user chose C2 as the next task, 2026-10-24).
C7 (a5fb6e1), C5 (24413e8) and C1 (0bf7257, c91eac2 + perf e2167dc,
79e1a2b, 089293a, baselines 19874cc) are committed. The parse runs in a
worker process (`compiler/parse_worker.h`; flags in docs/usage.md),
replaced once idle 60 s over 1024 MB. Its walks run on a walk thread
that is retired after a big walk, because malloc arena fragmentation
made later walks 2.5x slower (plan.md C1). Live UVM: edits take 12 s,
the same as in-process; 46 MB after idle instead of 6.6 GB.

What C2 needs (plan.md §6.33 F4, T1, T2, C2 for the numbers):
1. **Rebuild the profiling harness first.** The T2 scratch harness is
   gone (it lived in a scratchpad; nothing in `tools/`). It must:
   generate a parser from a copy of `grammar/Sv.g4`, parse a fixed UVM
   slice (preprocessed; T2 used 12.5k lines, baseline 29.7 s LL cold)
   and the whole of UVM, and print the time, the syntax-error count and
   the top decisions by `setProfile` (decision, rule, time share,
   invocations, LL fallbacks, SLL max lookahead). Also run it with SLL
   prediction: the C2 goal is **SLL-clean on UVM** (T1 today: 19,348
   SLL errors vs 1 real LL error). Consider committing it under
   `tools/` so later grammar changes can be checked the same way.
2. **Order of work** (by the profile): `primary` E1 -- drop
   `let_expression` and `sequence_method_call` from `primary` /
   `restricted_constant_primary`, and guard `randomize_call` with
   `{LT(1) == "randomize"}?` (-20% on the slice); E2 -- `tf_call`
   requires parentheses, and a paren-less call statement gets its own
   `subroutine_call` alternative (-26%); then the left-recursive
   `primary '.' method_call_body` loop (decision 1061, still 4.3 ms per
   prediction), `class_type` / `ps_class_identifier` (SLL lookahead
   368910 tokens), `subroutine_call`, `method_call_body`,
   `hierarchical_identifier`, `seq_block`. Measure after each step and
   keep only what pays.
3. **What must not be lost.** C2 must accept the same language; the
   parse tree is the walker's API (`src/compiler/sv_tree_walker.cpp`).
   - E2 risk: `enterTf_call` (sv_tree_walker.cpp ~1066) records bare
     calls for the missing-argument check (§6.23), **including
     paren-less ones** (`initial t;` -- test_sv_listener.cpp "a task call
     with no parens at all records zero argument slots"). A paren-less
     call statement must still produce a Tf_call (or the walker must
     record the new alternative). A paren-less call inside an
     expression (`x = f;`) would become a plain `hierarchical_identifier`
     and drop out of the check. Decide with the user whether that is
     acceptable (the LRM allows it only for functions with all-default
     arguments, so missing-argument hits there are rare) or record it
     another way.
   - References/rename find paren-less calls by text, not tree
     (test_references_scoped.cpp "task -- declaration and calls with and
     without parens"); keep that test green.
   - E1: the walker uses no `let_expression` / `sequence_method_call` /
     `randomize_call` context; still grep `SvParser::` in the walker for
     every rule a change touches.
   - Each change needs the unit suite, the Emacs suite, `make
     test-integration-uvm`, and then once per batch the UVM corpus (1563 /
     31 today; `--build-db` 1 diagnostic, 15731 symbol rows) and `make
     perf-check PERF_SCENARIOS="small uvm uvm_src"` (uvm_src ~25 min, ~7
     GB; baselines in `tests/perf/baselines/`). A grammar change
     regenerates the parser: a full rebuild of build/debug and
     build/release.
4. **Payoff.** Once SLL-clean: a two-stage parse (SLL first, LL on
   error) -- T1 measured SLL 114 s cold / 14 s warm vs LL ~850 s cold /
   409 s warm on UVM. Memory (ANTLR's static caches) should shrink with
   the lookahead, too; C1 already bounds it between compiles.

After C2, still open: C4 (references(`get_name`) 2.7 s on live UVM, over
the 1 s budget), and the two older bugs the latency benchmarks found:
library locations resolved against the server's cwd, and didChange
blocking the message loop during a compile.

What the worker changes for other work:
- Nothing in the server parses any more unless `--in-process-parse`; the
  ANTLR statics live in the worker. Unit tests and `--build-db` parse
  in-process (`CompilationController` without a walker).
- A new WalkResult / ParseRecord / ParseError / ... field must be added to
  the encoding in `parse_worker.cpp` (put/get pairs), or it is silently
  dropped by the worker; the round-trip unit test only covers fields the
  fixture sets.
- unit_tests depends on the svlsp target (SVLSP_EXE) for the worker tests.

Verified 2026-10-24 (C1 final): unit 1041/1041, Emacs 309/309, `make
test-integration-uvm` 13/13 (release), perf-check small + uvm no
regression, uvm_src as in plan.md. (C7 + C5: UVM corpus 1563 / 31, log
`build/release/corpus_2026-10-23.log`; the corpus parses in-process, so C1
doesn't touch it.)

Main findings: the library-DB workflow is cheap (55 MB, all p90 <= 15 ms);
the old "completion response size" item was Emacs GC (closed); a live
compile of UVM costs ~14 min and 6.8 GB **and the server keeps 6.6 GB**
-- all in ANTLR full-LL prediction and its static caches (the grammar's
`primary` ambiguities). C1 (done) bounds the server's resident memory;
C2 (next) is for the compile time.

For code reviews: `make perf-check` (small + uvm against
`tests/perf/baselines/`, same machine for timings); refresh a baseline
on the base commit with `make perf-baseline SCENARIO=...`.

What the receivers change (2026-10-21) needs other work to know:
- New kind **`Instance`** (module/interface/program instance; scope = the
  enclosing unit, detail = type, `$fixed_array:` per unpacked dim). It
  maps to SymbolKind Object / CompletionItemKind Module, shows in
  document symbols and completion. Not a declare-before-use subject.
- Signal/Port/Typedef/Function details now carry interface types:
  `intf` or `intf.mp` for `virtual [interface] intf[.mp]` and ANSI
  interface ports (`interface` for a generic one). Strip `.mp` before
  resolving (`resolveTypeName(..., valueType=true)` does).
- `Resolver::typeOfRow` returns `ReceiverType{owner, builtin}` (public in
  `symbol_resolution.h`; `resolveReceiverType` exports the chain walk).
  The owner can be a Class, a struct Typedef, an inline-struct
  variable/member, a design unit or a Block -- not only a Class.
- Dot completion uses `resolveReceiverType`; `resolveChain`,
  `resolveFirstSegment`, `resolveMemberSegment`, `membersAcrossChain` are
  deleted. `splitLayers` / `isContainerDimensionTag` are public in
  `symbol_utils.h`.
- A bare name exactly at a recorded declaration's position resolves to it
  (exact) even when no lexical chain reaches it (struct members).
- Disclosed: modports / clocking blocks not recorded (`vif.cb` -> nothing,
  `vif.cb.sig` -> fallback); no upward names (`$root`); completion after a
  module receiver skips its named blocks.

Finished and committed 2026-10-19 (4e8a10d): `typedef class` rows +
declare-before-use (plan.md
§6.30, "Follow-up: forward typedefs and declare-before-use", with its
*Performance* and *Verification* bullets). Forward typedefs are fallback
Typedef rows (detail = the keyword); `findSymbolsVisibleAt` ends with
`keepDeclaredBefore`, which drops names declared after the cursor in the
compilation unit's include order. Performance: an instrumented A/B showed
the filter's own time was the whole overhead (a `std::set` of copied
pairs for every row); after rewriting it to mark-then-compact with
forward-row-only key sets, references(`get_name`) on the corpus is
3.66 s -> 3.85 s (+5%, was +17%). Verified: unit 1016/1016, Emacs
295/295 (test_46), UVM corpus 1563 / 31 cases, `--build-db` +201 Typedef
rows (18440 symbols; corpus `uvm.db` swapped in, the previous one is
`uvm.db.v16.bak`), `make test-integration-uvm` 13/13.

What the 2026-10-15 session did on top of the 2026-10-13 tree:
- The 2 failing UVM corpus cases were not bugs. The `uvm_comp` prefix
  case's 21 "non-prefix" items are legitimate fuzzy (subsequence) matches
  from the rest of `uvm_pkg` (`uvm_port_component`, `UVM_TLM_COMPLETED`,
  ...), now visible through "Part 1b"; the case now checks that every
  item fuzzy-matches and prefix matches rank first. The empty-prefix
  case's anchor (line 59, column 0) is now before the class name, in the
  package: it moved into the class body (line 60, pinned 1042), and a new
  package-level case pins line 59 column 0 (795). Both lists diffed.
- Found by the `--build-db` check and fixed: an included file with no
  tokens of its own (UVM's wrapper files `tlm1/uvm_tlm.svh`,
  `comps/uvm_comps.svh`, ...) got include scope `''` even inside
  `package uvm_pkg`, so opening one would have compiled its includees
  unscoped. The walker now gives such a file the common prefix of the
  scope stacks at the tokens around its lines (`visitTerminal` /
  `scopeGapBefore`, seed scope at the ends); unit case "a file with no
  tokens of its own gets the scope it is included in".

Verified 2026-10-15: unit **1004/1004** (debug); Emacs **292/292**; UVM
corpus **1903 assertions / 31 cases**, all passing (log
`build/release/corpus_2026-10-15b.log`); `--build-db` symbols identical
(18239), v16, 165 include_sites rows (146 `uvm_pkg`, 19 file-level macro
files + `uvm_pkg.sv`); corpus `uvm.db` swapped in (the previous one is
`uvm.db.blocks.bak`); `SVLSP_BIN=$PWD/build/release/svlsp make
test-integration-uvm` **13/13** (probe-empty now 1067 items).

Committed 2026-10-08: block scopes (5bea2b8 + c14574c; plan.md §6.30,
"Follow-up: block scopes"). `begin`/`end`, `fork`/`join` and generate
blocks and for/foreach loops are scopes (kind `Block`, no schema change),
and loop variables are recorded. Verified: unit 986/986, Emacs 285/285
(test_44), UVM corpus 1147 / 30 cases (pinned symbol count 15731 ->
18239), `--build-db` +2508 rows (1872 Block, 636 loop variables; corpus
`uvm.db` rebuilt, the v14 one before blocks is `uvm.db.v14.bak`), `make
test-integration-uvm` 13/13.

**Latency benchmarks (2026-10-19, fc48120):** `tools/lsp_perf.py`
(svlsp alone) and `tests/perf/emacs_perf.sh` (through lsp-mode) time the
same scenarios (`tests/perf/{uvm,small}.json`) against budgets from the
response-time literature (`tests/perf/README.md`). UVM testbench on the
library DB: every request's p90 is under 100 ms in both; the slowest is the
1067-item empty-prefix completion (server 9 ms, 155 KB; Emacs 47 ms raw,
85-90 ms through capf). Edit -> diagnostics is 370 ms standalone, 860 ms in
Emacs (lsp-mode's breadcrumb flushes the delayed didChange at 0.5 s idle)
and **1.35 s with the breadcrumb off** (lsp-mode's 1.0 s full-sync delay)
-- over the 1 s flow budget; incremental sync would avoid that delay.
Found while writing them, both open:
- **Library locations point at the wrong file.** A library DB stores
  included files relative to their include dir (`base/uvm_component.svh`,
  every DB since v11), and providers absolutize them against the server's
  cwd: definition of `get_full_name` from a testbench returned
  `file:///home/martin/src/svlsp/base/uvm_component.svh`. The corpus tests
  hide it (`expectedUriPath` does the same transform). Likely also why
  references from a testbench find no library occurrences.
- **didChange blocks the message loop during a compile**: its handler
  takes the data mutex with an untimed `lock_guard`, so a request sent
  after it queues for the rest of the compile (typing at 300 ms/char:
  completion round trip 60-73 ms, server run 5 ms, `wait` 0). One compile
  at most (~55-70 ms here), but it grows with the file.

**Closed 2026-10-22: completion response size** (plan.md §6.33, F2/T3):
the server is fast (9 ms); the Emacs cost was garbage collection at the
default `gc-cons-threshold`; with 100 MB the 1067-item completion is
22 ms through capf. Compacting `data` saves < 1 ms -- not done.

Committed 2026-10-06: the small §6.30 follow-ups (e719dcd; plan.md
§6.30, "Follow-up: the constructor and out-of-class completion gaps") --
`return new(...)` and `new` passed as an argument resolve to the right
constructor (hover/definition/references/signature help), `super.`
inside a constructor offers `new`, and bare-name completion in an
out-of-class body in another file offers the class's own members.
Verified: unit 948/948, Emacs 276/276, `make test-integration-uvm` 13/13
(release), UVM corpus 1145 / 30 cases (unchanged). No walker change, so no
`--build-db` rerun.

Committed 2026-10-05/06: function/task arguments as dot receivers
(2fd1afa), the opt-in Emacs UVM completion test (7a784d9), docs
(1837759) -- plan.md §6.30, "Follow-up: a function/task argument is a
dot receiver" and "Emacs test against the UVM library DB". Verified:
unit 942/942, Emacs 273/273, `make test-integration-uvm` 13/13 (debug
and release), UVM corpus 1145 / 30 cases, `--build-db` changed only
in 190 Port details (dims added).
- Run the UVM Emacs test with `make test-integration-uvm`; profile with
  `SVLSP_BIN=$PWD/build/release/svlsp make test-integration-uvm`. Its
  default library DB is `<corpus>/uvm.db`, rebuilt 2026-10-19 with forward
  typedefs (v16 before them `uvm.db.v16.bak`; v14 with block scopes `uvm.db.blocks.bak`, before
  blocks `uvm.db.v14.bak`, v13 `uvm.db.v13.bak`, v11 `uvm.db.v11.bak`);
  rebuild it after any
  change to what the walker records.
- Port details now carry unpacked dims after `PARAM_DEFAULT_VALUE_SEP`:
  test for a default with `portHasDefault`, read the type with
  `portValueType`, display with `displayDetail` -- never test for the
  separator alone.

Committed 2026-10-05: dot completion filters `local`/`protected` members
by the cursor's class (f80648e + a2a7af6; its corpus case passed 2026-10-06).

Committed 2026-10-05: completion skips an ancestor's `local` members,
schema v13 (plan.md §6.30, "Follow-up: an ancestor's `local` members
aren't offered by completion"). Verified: unit 935/935, Emacs 272/272,
UVM corpus 1127 assertions / 28 cases (the empty-prefix case lost
exactly the 7 ancestor `local` members, 331 → 324, now pinned),
`--build-db` unchanged apart from size. Also fixed with it: test_39's
multi-level `::` case read diagnostics after a fixed `sit-for 2` while
its fixture compiles in ~2.7 s under ASan; it now polls (up to 15 s).

Committed 2026-10-04: the missing-argument check resolves callees with
`resolveSymbolAt` (plan.md §6.30, "Follow-up: the missing-argument check
resolves callees with `resolveSymbolAt`"). Committed 2026-10-01:
per-source diagnostics, schema v12 (plan.md §6.21, "Prerequisite done";
047c71d + c0a6300) -- a live edit no longer wipes `LibraryResolver`'s
"unresolved instantiation" diagnostics. Verified together: unit 930/930,
Emacs 271/271, UVM corpus 1132 assertions / 28 cases, `--build-db`
unchanged (see baselines below).

Committed 2026-10-01: signature help resolves callees with
`resolveSymbolAt` (72a0b75 + b651a58; corpus 1132/28 passing).

Committed 2026-09-30, in order: the whole-`::`-qualifier
fix with multi-level `::` Emacs tests (340f590 + 9d4a8da), then the
typedef-scope fix and helper merges (0ffe00c + 9dbee21).

In short: a call's recorded qualifier (`CallRecord::calleeScope`,
signature help's `CallHeader::scope`) is now the whole qualifier,
`T::type_id`, not just `type_id`, so `BUSTYPE::type_id::create` (type
parameter) fails closed instead of resolving to the calling file's own
`type_id`. Not covered: a bare `type_id::create(...)` in a file with
several classes still uses the same-file tie-break. Emacs tests for
multi-level `::` (none existed) were added with it, and exposed a resolver
gap now fixed too: a class-scoped typedef as a middle segment
(`pkg::C::type_id::create`) is followed to its class, and definition on
that `type_id` lands on the typedef. A review follow-up (plan.md §6.30,
"Follow-up: a typedef's aliased type resolved where the typedef is
declared") fixed `baseClassChain` resolving a typedef's bare aliased type
by name instead of from the typedef's scope, and merged duplicated helpers
(`SymbolDatabase::scopeChain`, `pickSameFilePreferred`,
`Resolver::classInScope`). Then signature help moved onto `Resolver`
(72a0b75 + b651a58).

§6.31 (doc comments on declarations) is done and committed (plan.md §6.31,
"Implementation").

- Committed on `main`: everything through §6.30 and §6.29, including
  macro hover/definition/references/rename, and the comma-shorthand
  parameter fix (`function f(input int a, b)` now records `b` as
  `input int`; LRM 13.3 inheritance in `enterTf_port_item`), and
  constructors (recorded, resolved by context), typedef aliases followed
  to their class, inherited members in bare-name completion, and
  signature-help trigger characters (`(`, `,`; retrigger `;`), macros
  in workspace symbols, no completion inside comments/strings, and doc
  comments (§6.31) in hover, signature help and completion resolve.
- Test baselines:
  - unit: **1041 cases**, all passing (2026-10-24, C1; 1040 before the idle policy; 1031 with C7; 1029 on the receivers tree;
    1016 at 4e8a10d) (no `[!shouldfail]` known gaps left
    -- the `` `define `` references case passes since the macro follow-up)
  - Emacs functional: **309/309** (2026-10-23, test_48 new; 305 with test_47)
  - UVM corpus (opt-in): **1563 assertions / 31 cases**, all passing
    (unchanged by the receivers change, 2026-10-22).
    (1903 -> 1563 with declare-before-use: in-class empty prefix
    1042 -> 714, package-level 795 -> 467, each dropped name checked to
    come later in include order; symbols pin 18239 -> 18440, docs 965.)
    (1147 -> 1903 with column-granular scopes + include context: the
    in-class empty-prefix list 325 -> 1042 (all of `uvm_pkg` visible),
    checked item by item, and a new package-level case at 795; diffed.)
    (+2: the `class` keyword in the empty-prefix list, 324 -> 325.)
    (+18: the dot-access case, 11, and the argument-receiver case, 7.)
    (1132 → 1127: the empty-prefix completion list lost the 7 ancestor
    `local` members, 331 → 324, diffed; then +2 for the pinned count's
    `m_inst_id`-absent / `m_inst_count`-present checks.)
    (+10: three §6.31 doc-comment cases -- prototype/body fallback, a
    tag-only method with no doc, and the corpus doc count pinned at 960.)
    (+16: the workspace-symbol macro case -- 4 fixed checks plus 2 per
    result for the 6 `uvm_info*` macros.)
    (1018 → 1106: +8 two definition cases -- typedef qualifier and
    `super.new`; +80 inherited items in the empty-prefix completion list,
    251 → 331, diffed.)
    (1031 → 1018 with constructors: the empty-prefix completion list lost
    13 leaked constructor arguments/locals, 264 → 251 items, diffed.)
    (499 at §6.30 step C → 388 at step D: the empty-prefix completion
    case checks each item and step D's (name, kind) dedup removed 124
    duplicates; +10 for §6.29 part A; +633 for the macro follow-up, whose
    references case checks each of its 313 locations.)
  - UVM corpus `--build-db`: **1 diagnostic, 18440 symbol rows (1872
    Block, 511 Typedef incl. 201 forward), 553 macro rows, 166 files, 165
    include_sites rows, 965 symbol docs + 30 macro docs; 5.32 MB (5320704
    bytes), 14 min / 6.9 GB RSS**; identical after the receivers change
    (2026-10-22) (v16 before forward typedefs: 18239
    rows, 5.31 MB, `uvm.db.v16.bak`) (v14 before the
    include context: 163 files, 5.27 MB; before block scopes: 15731 rows,
    4.51 MB; 4.30 MB with `--no-doc-comments`) (~17 min, ~7 GB RSS; don't run it at the same
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

**What block scopes (§6.30 follow-up) need other work to know:**
- Kind `Block`: a scope row, never a declaration a user points at.
  Named by its label, else `@<line>.<col>` of its first token; locals
  inside are scoped `m::f::@12.4` / `m::t::blk`. Anything listing rows to
  a user must skip Block (completion, document/workspace symbols do), and
  show a scope through `displayScope` (drops `@` segments).
- `scopeAtPosition` includes blocks; pass `withBlocks = false` for the
  enclosing unit/class/subroutine. `scopeKindAtPosition` never reports
  Block. Code that tests the innermost scope's last segment (like
  "inside `new`") must use the `false` form or walk the chain.
- Walker: blocks push frames through `enterBlock`/`exitBlock`
  (`m_blockFrames`), with `end_line` set at enter -- not
  `backpatchEndLine`.

**What scoped imports (schema v14, §6.32) need other work to know:**
- `imports.scope` / `ImportRecord::scope` / `ImportRow::scope`: the scope
  chain the import is written in ("" = compilation unit). Anything that
  reads imports must keep only those whose scope is on the position's
  scope chain (`findSymbolsVisibleAt`, `Resolver::lookupBare`), and a
  package's exports are the rows scoped to that package
  (`collectExportedImports`). A pre-v14 row has scope "" (file-wide).
- `scopeAtPosition`/`scopeKindAtPosition` include Program now.
- No comma in a schema comment directly above a column a migration test
  drops: SQLite's `DROP COLUMN` scans back to a comma to find the column.

**What the class-member access change (schema v13) needs other work to know:**
- `symbols.access` / `ParseRecord::access`: 'local', 'protected' or ''
  on a class member's own row (variables, the method or its extern
  prototype; never arguments/locals, never an out-of-class body row).
  `SymbolRow` does NOT carry it: read it with
  `SymbolDatabase::localMemberNames(scope)` (per class scope, by name,
  across attached DBs; a pre-v13 library DB contributes none). Adding it
  to the shared row projection would break attaching older library DBs.
- A schema migration test that reshapes the current DB down to an older
  version keeps newer columns, so each migration that adds a column
  skips the `ALTER` when it's already there (v12, v13, v14).

**What the missing-argument resolver change needs other work to know:**
- `CompilationController` runs the missing-argument check only with a
  callee resolver set: `setCalleeResolver(resolveCallees)`
  (`lsp/symbol_resolution.h`). A new controller in production code (or
  a test that expects missing-argument diagnostics) must set it.
- `SymbolDatabase::resolveMethod` is gone; `baseClassChain` stays
  (completion, `symbol_utils`).

**What per-source diagnostics (schema v12) changed that other work must know about:**
- `diagnostics.source` (`'compile'` default, `'library'`) and
  `diagnostics.subject`. `replaceDiagnostics(fileId, errs, source =
  "compile")` deletes only that source's rows; a new diagnostic producer
  picks its own source value rather than appending untagged rows.
- `'library'` rows are re-anchored on every recompile
  (`refreshLibraryDiagnostics`) and are part of `compile`'s return value.

**What §6.31 changed that other work must know about:**
- Schema **v11**: `symbol_docs(file_id, line, col, doc)` (WITHOUT ROWID,
  written by `replaceSymbols`) and `macros.doc`. Read a symbol's doc with
  `symbolDoc(db, row)` (symbol_utils; prototype/body fallback), not
  `db.docFor` directly, unless you want exactly that row's.
- `ParseRecord::doc` / `MacroRecord::doc`; `pushIdInScope` now takes the
  declaration ctx the doc is found around (null = no doc).
- A comment block directly above a declaration IS its doc now — a test
  fixture whose header comment sits right above a declaration changes that
  declaration's hover (test_26 hit this). Leave a blank line after a
  fixture's header comment.
- Completion items for DB symbols carry `data` {path, line, col, kind,
  scope}; the server advertises `resolveProvider` and answers
  `completionItem/resolve`.
- `CompilationController::setCollectDocs(false)` — from
  `initializationOptions.svlsp.docComments: false` or `--build-db
  --no-doc-comments` (which also tags the recorded build version, forcing a
  rebuild on a switch). Lazy `libraryDbSources` builds always collect docs.

**What the constructor follow-up changed that other work must know about:**
- Function rows named `new` now exist (scope = the class, body frame
  `<class>::new`). `new` is resolved only by context
  (`resolveNew` in `symbol_resolution.cpp`), never by name; completion
  skips these rows; rename refuses them; `overrideFamilyId` never merges
  constructors across a hierarchy.
- `T x = new(...)` / `v[] = new[n]` declarations are recorded now (they
  parse through `class_variable_identifier` /
  `dynamic_array_variable_identifier`): use `declaredVariable(vda)` in the
  walker, never `vda->variable_identifier()` directly.

## Next up — candidates

- The "Other open work" list below.

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

0. **§6.30 leftovers** (all disclosed limits are closed once the
   receivers change is committed): modports and clocking blocks as
   symbols (`vif.cb.sig`), upward hierarchical names, named blocks in
   completion after a module receiver.
1. **§6.21** semantic reference-resolution diagnostics (unresolved type/import)
   — not started; its prerequisite (per-source diagnostics, v12) is done --
   use `source = 'reference'` with `replaceDiagnostics(..., source)`.
2. **§6.24** audit compiler warnings from `tools/build.sh` — not started.
3. **§6.12** configurable debounce interval (`debounceMs`) — not started.
4. **§6.20** external read-only DB access — not started.
5. **§6.22 follow-up**: UVM-corpus coverage for rename (references has
   it since §6.30 step B, `tests/uvm_corpus/test_references_uvm.cpp`).
6. **§6.19** library-DB staleness detection (source changed since build) —
   open question, no shape chosen.
7. **§6.5** performance — open; see "Known gaps" for the concrete costs.
8. Minor: comment at `pathToUri()` explaining that `fromPath()` always
    absolutizes.
9. **Open question (raised 2026-10-08): should the missing-argument
   diagnostic stay strictly in scope?** Today `resolveCallees`
   (`src/lsp/symbol_resolution.cpp`, the `byName` /
   `pickSameFilePreferred` block) checks an *unqualified* call that
   scoped resolution can't resolve exactly against any same-named callable
   (same file first), so `f(x)` with no `f` in scope can be checked
   against another file's `f`. A `::`-qualified call already fails closed,
   and completion never matches by name. Dropping the fallback makes the
   diagnostic scope-bound like completion, at the cost of no
   missing-argument check at all on calls the resolver can't see (e.g. a
   callee in a library DB or a file not compiled yet). Decide with the
   user before changing; if changed, check the UVM corpus `--build-db`
   diagnostic count (1 today), test_39 and the missing-argument cases in
   `tests/unit/db/test_compilation_controller.cpp`.

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
                   library_resolver, project_compiler, schema (v14)
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
make test-integration-uvm                     # opt-in Emacs UVM completion + timings (SVLSP_BIN to profile release)
make perf-lsp [SCENARIO=uvm|small|uvm_src]   # latency + memory benchmark, svlsp alone (tests/perf/README.md)
make perf-check [PERF_SCENARIOS="small uvm"] # regression check vs tests/perf/baselines/ (code reviews)
make perf-baseline SCENARIO=...              # refresh a baseline (on the base commit)
DISPLAY=:99 make perf-emacs [SCENARIO=...]   # the same scenario through Emacs + lsp-mode

cmake --build --preset release --target uvm_corpus_tests && ./build/release/uvm_corpus_tests
                                              # corpus root: SVLSP_UVM_CORPUS_DIR
                                              # (default /home/martin/src/verilator_test/uvm-core/src)

build/release/svlsp --build-db <config.f|.svlsp.json> --output <lib.db>
build/release/svlsp --log-files /path/to/log.txt
build/release/svlsp --log-requests /path/to/requests.log   # per-request server time
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
| Database | SQLite3 amalgamation, schema v16 |
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
| Hover / Definition | `resolveSymbolAt` (`src/lsp/symbol_resolution.h`, plan.md §6.30): `::` qualifiers, named connections, `.` receiver chains (class, struct, interface, instance, block), bare names innermost-scope-first; `exact=false` name-only fallback (`findSymbolsByName` + `pickBestSymbol`) when the qualifier/receiver isn't understood |
| Completion | `findSymbolsVisibleAt` + keywords (`sv_keywords.h`, scope-kind legality) → fuzzy scoring (toggle: `initializationOptions.svlsp.fuzzyCompletion`). Dot-completion: `dotCompletionContext` → `resolveReceiverType` (the resolver's chain walk) → a class's members across `extends`, a struct/interface/module/block owner's own rows, or builtin container methods via `sv_builtin_methods.h` |
| References / Rename | `ReferencesProvider::findOccurrences`: lexical cross-file `findIdentifierOccurrences`, each hit resolved with `resolveSymbolsAt` and kept if it lands on the target row / same method override family (`overrideFamilyId`), or only resolves by fallback. Rename edits exactly that set |
| Signature help | instantiation ports; bare / `Class::` / `pkg::` / dotted calls resolved with `resolveSymbolAt` (exact Function/Task only; a qualified or dotted call fails closed, an unqualified one nothing visible declares falls back to a name-only search). Labels from `Port.detail` (`"<dir> <type>"`, optional `\x1F<default>` suffix) |

Chain helpers (`peelDimensionLayers`, `splitLayers`,
`dotCompletionContext`, `positionForOffset`) live in
`src/lsp/symbol_utils.h/.cpp`; receiver chains are resolved only by
`Resolver` (`symbol_resolution.cpp`).
`svlsp_db` must not depend on `svlsp_lib`, which is why `svlsp_db` code
uses `SymbolDatabase::pickSameFilePreferred` (the simpler half of
`pickBestSymbol`). Scope chains (innermost first, ending `""`) come from
`SymbolDatabase::scopeChain`.

### Encodings in `symbols.detail`

- Class: single parent class name (`extends`); `SymbolDatabase::baseClassChain`
  walks it (cycle-guarded).
- Signal/Parameter: `:`-joined type layers, outermost first, with `$`-prefixed
  container tags (`$queue`, `$assoc_array`, `$dynamic_array`, `$fixed_array`,
  `$string`, `$event`) — e.g. `"$fixed_array:$queue:MyClass"`.
- Port: `"<direction> <type>"`, optionally `"\x1F<default>"`
  (`PARAM_DEFAULT_VALUE_SEP`); interface ports store only their header text
  (`intf.mp`, `intf`, `interface`).
- Instance: the instantiated type, after `$fixed_array:` per unpacked dim.
- A virtual interface's type is `intf` or `intf.mp` (Signal, Typedef,
  Function return type).
- `""` as a *scope* means top level — `findSymbolsInScope("")` returns every
  top-level symbol, so guard against an empty resolved type.

### Database (`src/db/`, schema v14)

Tables: `files`, `symbols (kind, name, line, col, parent, detail, end_line,
scope, access)`, `diagnostics (… source, subject)`, `symbol_docs`, `imports (pkg_name, item, is_export, scope)`,
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
- Completion probes (`probe: w.` text at a fixed position) go on lines
  inside a never-defined `` `ifdef SVLSP_TEST_PROBES `` ... `` `endif ``,
  never in a `//` comment: completion offers nothing inside a comment or
  string, and bare probe text on a code line gets parsed as a declaration.
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
  anything that only queries `symbols` misses them (workspace symbols
  queries both since 2026-09-27).
- Covergroup methods and `randomize() with {…}` constraint completion
  unsupported.
- Doc comments (§6.31): `int a; int b;` on one line gives `b` no doc; no
  NaturalDocs formatting; not shown in workspace/document symbols; lazy
  `libraryDbSources` builds ignore `docComments: false`.
