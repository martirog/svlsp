# svlsp — Handoff Document

**Date:** 2026-07-15  
**Last completed phase:** Phase 6.2 complete — multi-file project support, all 6 stages
(6.1 — DB-backed providers; 6.3 — package import *and export* resolution; preprocessor
source map committed previously)  
**Current work (2026-08-21, see "UVM real-world smoke test — session 3" section below,
near the top, for full detail):** Continuing the UVM real-world smoke test. This
session finally got the **entire real UVM corpus (140 files, ~85K preprocessed lines)
compiled end-to-end through the actual production pipeline** (`CompilationController`
— the same code path the real `svlsp` LSP server uses) for the first time ever —
previous sessions only ever measured preprocessor-only errors on the full corpus, or
ANTLR parse timing on isolated single files. It completes in **~420-480s (~7-8 min)**,
not "many minutes" indefinitely as feared — confirms the Phase 6.5 performance
finding from session 2 was real but not fatal. Result: **2956 diagnostics across 73 of
140 files**. Root-caused the dominant patterns to two causes: (1) **Gap C
(stringification) has far higher real-world impact than session 1 estimated** — it's
triggered by `` `uvm_object_utils ``/`` `uvm_component_utils ``, the single most
common factory-registration macro pair in all of UVM (not just the rarer
`` `uvm_field_* ``/token-pasting sites session 1 measured), via
`` `m_uvm_object_registry_internal ``'s `` `uvm_type_name_decl(`"T`") ``. (2) **New
finding, not preprocessor-related at all**: `grammar/Sv.g4`'s `STRING_LITERAL` lexer
rule (`'"' .*? '"'`) has **no escape-sequence handling** — any string literal
containing an embedded `\"` (extremely common in `` `uvm_error ``/`` `uvm_report_info ``
message strings that quote a name/value) terminates prematurely, corrupting
tokenization for the rest of the line. Also confirmed real-world impact of the
already-documented `void'(...)` cast gap (pervasive in `uvm_root.svh` etc.). On the
LSP layer: all 28 candidate deep UVM symbols and all 5 `documentSymbol` queries
resolve correctly; but found a **real symbol-table-pollution bug** — Gap C garbage at
`uvm_report_catcher.svh:71` creates a bogus `Signal` symbol literally named
`uvm_report_object`, which silently shadows the real `Class` at
`uvm_report_object.svh:98` in hover/definition (no kind preference, alphabetical file
ordering) — a silent-wrong-answer bug, not a crash. Also confirmed (separately from
this session, still true): `didOpen` only ever publishes diagnostics for the
**primary** opened file, never transitively-`` `include ``d files — a real LSP
diagnostics-visibility gap.
**Update, later same session — three fixes, all committed:** the `STRING_LITERAL`
escape gap (`b5cda7c` — `grammar/Sv.g4:3795`, `'"' ( '\\' . | ~["\\] )* '"'`; tag
`[stringescape]`; **2956 → 1796, −39%**), Gap C stringification (`5a41322`/
`aca4eb1` — `src/compiler/sv_preprocessor.cpp`'s new `tryStringify`; tag
`[stringify]`; **1796 → 1401, −22% further**), and the already-documented
`void'(...)` cast gap (`88f1610`/`9cfd7a8` — `grammar/Sv.g4`'s
`subroutine_call_statement`, `SINGLE_QUOTE` now optional; tag `[voidcast]`;
**1401 → 1254, −10.5% further**). **Cumulative: 2956 → 1254 diagnostics, −57.6%,
files-with-diagnostics 73 → 51**, full unit suite 904/359, zero regressions
throughout. Token-pasting (` `` `) remains unimplemented by design — root-caused
as the dominant remaining cause (three distinct real macro families found:
`` `uvm_register_cb ``, `` `M__TABLE_Q ``/`` `M__TABLE_GET ``, plus the original
session-1 sites) — a materially bigger feature than any of the three fixes above,
flagged for an explicit future decision rather than attempted without one. Gap E
and the symbol-pollution bug are still **not** fixed. See "Gap C stringification
— FIXED" and "`void'(...)` cast gap — FIXED" in the session 3 section below for
full detail.
**Not yet done:** decide on full token-pasting support (see session 3's "Not yet
done" list, item 4), then Phase 6.4 (cross-file
invalidation / dependency graph) per `plan.md §6.4` — not yet planned in file-level
detail. **The full approved Phase 6.2 plan (exact signatures, schema SQL, algorithms,
test names — now historical reference, all 6 stages complete) lives at
`/home/martin/.claude/plans/fluffy-hatching-popcorn.md`.**
See "Phase 6.2" section below for what was built in each stage.

---

## UVM real-world smoke test — session 3 (2026-08-21, full-corpus parse + exploratory LSP queries)

Direct continuation of session 2 below (paused 2026-08-20), resuming exactly where
its "Not yet done" list left off. Explicit user instruction: "resume UVM realworld
test", then later "keep going, update the handoff when done" — i.e. investigate as
far as productive and record findings, not necessarily fix anything yet.

### Setup

Rebuilt the `release` preset (picked up Gap A/D fixes from commits `9b8abed`/
`5ba19f9`, already current — no recompilation needed, confirming those commits were
already built). Recreated the JSON-RPC driver (`lsp_driver.py`, session scratchpad,
not committed — session 2 predicted this would need recreating and it did) with two
additions beyond session 2's design: an interactive stdin phase that can send
further typed LSP requests (tagged with `_tag` for correlation) after the initial
`didOpen` completes, against the *same still-running server process* — letting a
single ~8-minute corpus load be reused for many follow-up queries instead of paying
the load cost per query — and a `_notify` flag for fire-and-forget notifications
(`didOpen` on secondary files) alongside request/response pairs.

### Part 1 — Full UVM corpus through the real LSP server (item 1 of session 2's "not yet done" list)

Ran `build/release/svlsp` via the driver: `initialize` → `initialized` → `didOpen` on
the real `uvm.sv` (project manifest unchanged from session 1/2, at
`/home/martin/src/verilator_test/uvm-core/src/.svlsp.json`) → wait for
`publishDiagnostics`. **Completed in 421-477s (~7-8 min) across three separate runs**
(not a hang, not "many minutes" indefinitely — the session-2 Phase 6.5 performance
concern was real but bounded, at real full-corpus scale, to single-digit minutes).
`publishDiagnostics` for `uvm.sv` itself reported **0 diagnostics** — this is
expected and uninteresting, not a fix confirmation: see "LSP diagnostics-visibility
gap" below for why the primary file alone was never going to show anything.

### Part 2 — What the corpus actually contains: standalone `CompilationController` probe

Session 2's probes only ever exercised the preprocessor or the ANTLR parser
*in isolation*; nobody had run the **full production pipeline** —
`CompilerDirectiveStripper` → `SvPreprocessor` → `SvTreeWalker` →
`SymbolDatabase` persistence, i.e. exactly what `CompilationController::compile`
(and therefore the real server) does — over the *entire* corpus in one pass, because
session 2 judged even a 5-file bounded subset too slow to be worth the wait (>60s
without finishing, standalone ANTLR-only, no DB persistence). This session did it
anyway, since the LSP-driver run above proved the whole thing finishes in single-digit
minutes end-to-end.

**Probe** (`probe_diag.cpp`, session scratchpad, not committed — standard
`libsvlsp_db.a` + `libsvlsp_compiler.a` + `libsvlsp_antlr4.a` + antlr4-runtime +
`libsvlsp_sqlite3.a` static-link technique from session 1/2, extended to also link
`svlsp_db` and use `Database`/`SymbolDatabase`/`CompilationController` directly):
constructs an in-memory `Database`, calls
`CompilationController::compile("uvm.sv", text, &cfg)` with the same
`includeDirs: ["."]`/`UVM_NO_DPI` config as the real manifest, then queries the
resulting DB directly — total files, per-file diagnostic counts, distinct message
patterns, and `findSymbolsByName` lookups for all 28 candidate symbols.

**Results** (three separate runs, consistent: 430-478s compile time):
- **140 files** land in the DB (1 primary + 139 transitively `` `include ``d).
- **2956 diagnostics total, spread across 73 of the 140 files** (67 files fully
  clean). Worst offenders: `reg/uvm_vreg.svh` (330), `reg/sequences/
  uvm_reg_mem_shared_access_seq.svh` (201), `base/uvm_resource_pool.svh` (181),
  `reg/uvm_reg.svh` (151) — the `reg/` (register-abstraction-layer) subtree is
  disproportionately hit, consistent with it being both class-registration-macro-heavy
  (see root cause 1) and message-string-heavy (root cause 2).
- **All 28/28 candidate symbols found** via `findSymbolsByName`, each resolving to
  its correct declaring file:line exactly as catalogued in session 2's candidate
  list — confirms the corpus's *symbol* extraction is intact overall even though a
  majority of files have parse errors somewhere (ANTLR's error recovery keeps
  producing a mostly-usable partial tree around the errors, not abandoning the whole
  file).

### Root cause 1 — Gap C (stringification) impact was significantly underestimated in session 1

Session 1's original Gap C writeup judged real-world impact "low" based on counting
only direct `` `uvm_field_* `` invocations (17) and files referencing any
`` `uvm_*_utils `` macro (43) — but never traced what `` `uvm_object_utils ``/
`` `uvm_component_utils `` *themselves* expand to. They do not use token-pasting, but
they do chain into stringification: `` `uvm_object_utils(T) `` →
`` `m_uvm_object_registry_internal(T,T) `` → (`macros/uvm_object_defines.svh:555`)
```
typedef uvm_object_registry#(T,`"S`") type_id;
```
`` `"S`" `` is exactly the stringification form `SvPreprocessor`'s own doc comment
already declares unsupported (see Gap C in session 1's section below) — so the
literal `` ` `` characters survive into the parser's input, producing immediately
this pattern (verified against `reg/uvm_reg.svh:52`, `base/uvm_phase.svh:614`,
`base/uvm_packer.svh:62`, `reg/uvm_vreg.svh:354`, `reg/sequences/
uvm_reg_mem_shared_access_seq.svh:78` — every one is a `` `uvm_object_utils(T) ``/
`` `uvm_object_param_utils(T) ``-family invocation, confirmed by reading the actual
source at each cited line):
```
no viable alternative at input 'uvm_object_registry#(<T>,`'
mismatched input '#' expecting {';', '['}
mismatched input '`' expecting IDENTIFIER
extraneous input '`' expecting {...}     (×2 per site — one per `` ` `` in `` `"S`" ``)
```
**Corrected assessment: this is not a rare edge case.** `` `uvm_object_utils ``/
`` `uvm_component_utils `` (or their `_begin`/`_param` variants) are the standard
factory-registration idiom used in **every** UVM class that participates in the
factory — which is most of them. This single macro chain plausibly accounts for a
large fraction of the 73 affected files and a meaningful share of the 2956
diagnostics (each site produces ~5 immediate diagnostics before resynchronizing, not
counting further cascade). Still not fixed — fixing it means implementing
stringification (`` `" ``) in `SvPreprocessor`, which is a real (if bounded) feature
addition, not a quick patch; see Gap C's original note in session 1's section for the
"needs a slang-backed implementation" caveat, though a minimal stringification-only
implementation (converting `` `"...`" ``/`` `"ident`" `` spans to a quoted string
literal, without full token-pasting) may be tractable on its own.

### Root cause 2 — NEW: `STRING_LITERAL` lexer rule has no escape-sequence handling

`grammar/Sv.g4:3795`:
```
STRING_LITERAL : '"' .*? '"' ;
```
A non-greedy "shortest string between two `"` characters" rule — it has **no
awareness of backslash-escapes at all**. Any string literal containing an embedded
`\"` (e.g. `` `uvm_error("ID", $sformatf("Virtual register \"%s\" cannot have 0
bits", name)) ``, `reg/uvm_vreg.svh:435`) causes the `STRING_LITERAL` token to
terminate at that first embedded `"` (the preceding `\` is just an ordinary
character to this rule), leaving the remainder of the intended string
(`` %s\" cannot have 0 bits" ``) to be re-tokenized as ordinary code — producing
long, confusing cascades of `mismatched input ',' expecting '.'` /
`extraneous input ')' expecting ';'` / `no viable alternative` diagnostics for
the rest of that physical line (macro-expanded lines are long, so the fallout can
span many "diagnostics" per single root cause). Directly confirmed at two
independent, unrelated sites: `reg/uvm_vreg.svh:435` (inside a `` `uvm_error ``
call) and `base/uvm_root.svh:600` (inside `uvm_report_info(...)`, note: *not* inside
any macro invocation at all here — this is a plain function call, so this is a pure
grammar/lexer gap, unrelated to any preprocessor macro-argument-parsing gap like
Gap A). This is almost certainly the **second largest contributor** to the 2956
diagnostics, likely larger than root cause 1 in raw diagnostic count given how
common quoted sub-strings are inside UVM's own message-formatting calls (which use
this pattern constantly for good, readable error/log messages) — not yet counted
precisely, but the `mismatched input ',' expecting '.'` pattern alone (600
occurrences, the single largest message-pattern bucket) is consistent with this
being the dominant cause: after a corrupted string literal, the parser sees a
sequence of comma-separated "expression-like" tokens where it expects member-access
`.` chains, which is exactly the shape produced by re-tokenizing text like
`","test_name,"` as separate tokens instead of one string literal.
**Not fixed this session.** This is a real, independent grammar/lexer bug (not a
preprocessor limitation, not previously documented anywhere in this file) — fixing
`STRING_LITERAL` to support at minimum `\"` (and ideally the other standard SV
string escapes: `\\`, `\n`, `\t`, `\%03o`, etc., per LRM §5.9) is likely the
**single highest-value next fix** found this session: cheap (one lexer-rule change,
`'"' ('\\' . | ~["\\])*? '"'` or similar), and unlike Gap C it requires no design
discussion — it's an unambiguous lexer correctness bug with clear, pervasive
real-world impact.

### Confirmed (not new) — `void'(...)` cast gap has heavy real-world impact

The `extraneous input ''' expecting '('` pattern (135 occurrences, 3rd-largest
bucket) is the **already-documented** grammar quirk from the "Sv.g4 grammar quirks"
table further down this file (`void'(f())` not supported, workaround `void(f())`).
Confirmed at `base/uvm_callback.svh:219`, `base/uvm_root.svh:916/941/945`, and
others — all genuine `void'(...)` casts in real UVM code (e.g.
`void'(clp.get_arg_matches(...))`). Not a new finding, but confirms this
fixture-only-tested gap has real, pervasive real-world impact and is a plausible
low-effort fix candidate alongside root cause 2 above (both are pure Sv.g4 grammar
changes, no preprocessor design work needed).

### Part 3 — Exploratory LSP-layer queries (item 3 of session 2's "not yet done" list)

Reused the same driver's interactive stdin phase (`gen_uvm_queries.py`, session
scratchpad, not committed) against the live server process from the same `didOpen`
run that produced Part 1's result (i.e. these queries ran against the identical
140-file, 2956-diagnostic DB state characterized in Part 2 — same corpus, same
in-process run, not a separate reload).

- **`workspace/symbol` for all 27 remaining candidate names** (28th, `uvm_component`,
  covered separately below): all returned results, all in the expected files —
  e.g. `uvm_reg` → 71 results (itself plus every `uvm_reg_*` prefix match across the
  whole `reg/` subtree — `workspace/symbol` is prefix-ish/substring by design per
  existing behavior, not a bug), `uvm_analysis_port` → 1 exact result in the correct
  file, `uvm_tlm_generic_payload` → 2 results both in `uvm_tlm2_generic_payload.svh`
  (real file, name doesn't match its own class name exactly — `tlm2` vs `tlm` in the
  path — worth knowing if a future test hardcodes path-from-name assumptions).
- **`documentSymbol` for all 5 candidate files, queried by path without opening
  them** (per Phase 6.1's DB-direct design — confirmed still true at this scale):
  `base/uvm_component.svh` → 387 symbols, `base/uvm_object.svh` → 87,
  `seq/uvm_sequence_item.svh` → 74, `reg/uvm_reg.svh` → 414, `tlm1/
  uvm_analysis_port.svh` → 14. All non-empty, all plausible (uvm_reg.svh and
  uvm_component.svh are the two largest/most complex classes in the whole library,
  matching their symbol counts being the two largest here).
- **Hover/definition, requiring an open buffer (`m_store.contains` check) — two
  cross-file base-class resolution tests:**
  1. `base/uvm_component.svh:59`, `` uvm_component extends uvm_report_object `` —
     hovering `uvm_report_object` **returned the wrong symbol**: a `Signal` at
     `base/uvm_report_catcher.svh:71`, not the real `Class` at `base/
     uvm_report_object.svh:98`. See "New bug: symbol-table pollution" below — this
     is a real, reproducible-in-real-code bug, not a fixture artifact.
  2. `seq/uvm_sequence_item.svh:52`, `` uvm_sequence_item extends uvm_transaction ``
     — hovering `uvm_transaction` **correctly** resolved to `**Class**
     \`uvm_transaction\` → \`uvm_object\`` at its real declaration,
     `base/uvm_transaction.svh:138`. Positive control: proves cross-file,
     no-collision base-class hover/definition still works correctly at real-world
     scale — the bug in (1) is specifically a name-collision problem, not a general
     regression.

### New bug — Gap C garbage pollutes the symbol table, causing silent wrong hover/definition answers

Root cause, confirmed by reading the actual source: `base/uvm_report_catcher.svh:72`
contains
```
`uvm_register_cb(uvm_report_object,uvm_report_catcher)
```
— a **token-pasting** macro (a genuine Gap C site, distinct from the
stringification sub-case in root cause 1 above; this one really does use
`` T``CB `` internally). Since token-pasting isn't implemented, the broken expansion
leaves stray text that `SvTreeWalker` parses into a bogus symbol: a `Signal`-kind
record literally named `uvm_report_object` at that file/line — coincidentally the
exact same name as the real `class uvm_report_object` declared at `base/
uvm_report_object.svh:98`.

`HoverProvider::getHover` / `DefinitionProvider::getDefinition` (`src/lsp/hover.cpp`,
`src/lsp/definition.cpp`) call `SymbolDatabase::findSymbolsByName(word)`, which
orders results `ORDER BY f.path, s.line` (`src/db/symbol_database.cpp`) — pure
alphabetical-by-path, **no kind preference** (e.g. Class over Signal) and no
"is this actually a declaration vs. macro-expansion garbage" signal. `hover.cpp`
then does "prefer same-file match; otherwise use the first match" — and since
`uvm_component.svh` (where the hover was requested) declares neither symbol, no
same-file preference applies, so it silently takes `rows.front()`. Because
`"base/uvm_report_catcher.svh"` sorts alphabetically before
`"base/uvm_report_object.svh"` (`'c' < 'o'`), **the garbage symbol wins**, and the
user gets a plausible-looking but completely wrong hover result with **no
diagnostic anywhere indicating anything is off** — the file with the garbage symbol
(`uvm_report_catcher.svh`) does have 5 real diagnostics elsewhere (from this same
Gap C site's parse fallout), but the diagnostic and the wrong-hover-answer are not
obviously connected from a user's perspective.

**Why this matters beyond "Gap C causes parse errors" (already known):** this shows
Gap C's damage isn't confined to the file it occurs in — it can silently corrupt
*lookups for an unrelated, correctly-declared symbol in a completely different
file*, with no error surfaced to the user. This is a strictly new category of
finding this session (symbol-table integrity, not parse coverage), independent of
whatever the eventual Gap C fix looks like. **Not fixed this session** — flagging
as a real bug for the next session to consider, either as part of a Gap C fix (which
would eliminate the garbage symbol at the source) or as a defense-in-depth
improvement to `findSymbolsByName`/hover's disambiguation (e.g. prefer `Class`-kind
results, or exclude symbols from files with diagnostics at that exact line).

### LSP diagnostics-visibility gap (confirmed, not new behavior, but not previously written down)

Traced through `src/lsp/server.cpp`'s `didOpen`/`didChange` handlers and
`src/db/compilation_controller.cpp`'s `compile()`: the notification sent to the
client is built from `parseDiagnostics()`, which returns only
`m_compiler.compile(...)`'s return value — and `compile()` (`compilation_controller.cpp`
line ~85) returns `errsByFile[""]`, i.e. **only diagnostics attributed to the
primary opened file**. Diagnostics for every transitively-`` `include ``d file are
computed, partitioned by file, and persisted via `replaceDiagnostics(incFid, ...)`
in the same function — but nothing ever calls `m_diagnostics.publish()` for those
included files' URIs. A user opening `uvm.sv` (a 35-line wrapper) sees "0 problems"
in their editor even though the DB holds 2956 diagnostics across 73 included files.
This is why session 2's "1,241 diagnostics" and "335 → 173 → 86" progress tracking
always used direct preprocessor/DB probes, never the LSP protocol surface — the LSP
surface was never going to show them. Not a bug in the sense of "wrong behavior for
what's implemented" (documentSymbol/hover/definition all correctly reach into
included files' data via direct DB queries, as designed), but a real **gap**: there
is currently no way for an LSP client to discover that an included file has
diagnostics without separately opening that exact file itself. Worth a design note
for whoever picks up Phase 6.4 (cross-file invalidation) — that work will need to
reason about included-file diagnostics anyway.

### Verification / cleanup

- Release build confirmed current (Gap A/D commits `9b8abed`/`5ba19f9` already
  built; `cmake --build --preset release` was a no-op rebuild).
- Deleted `/home/martin/src/verilator_test/uvm-core/src/svlsp_subset_probe.sv` (the
  bounded 5-file fallback fixture prepared in case the full-corpus run didn't finish
  in time — it did, so the fallback was never used).
- All scratchpad tooling (`lsp_driver.py`, `probe_diag.cpp`, `gen_uvm_queries.py`,
  `uvm_queries.jsonl`) lived only in the session scratchpad, not committed — per
  session 1/2 precedent, recreate from the descriptions above if needed for a future
  session; exact paths won't survive to a new session.
- No production code changed this session — investigation/measurement only, per
  explicit user instruction ("keep going, update the handoff when done" — understood
  as "keep investigating," not "keep fixing").

### `STRING_LITERAL` escape-sequence gap — FIXED (2026-08-21, later same session, uncommitted)

Per explicit user request ("fix the STRING_LITERAL escape gap"). `grammar/Sv.g4:3795`:
```
- STRING_LITERAL : '"' .*? '"' ;
+ STRING_LITERAL : '"' ( '\\' . | ~["\\] )* '"' ;
```
The new rule: an escaped-anything alternative (`'\\' .` — consumes a backslash plus
whatever follows it, including a `"`, without treating it as the terminator) or any
ordinary non-quote/non-backslash character, repeated, still bounded by a real
(unescaped) closing `"`. No longer non-greedy — doesn't need to be, since the
negated char class already excludes the closing quote, so it can't over-consume.

**Unit tests** (`tests/unit/compiler/test_sv_parser.cpp`, tag `[stringescape]`,
4 new cases, following this file's existing `parseErrors(src)`-helper convention):
an escaped quote (reproducing the real `reg/uvm_vreg.svh:435` pattern without the
macro layer, since `STRING_LITERAL` is a pure lexer rule and macros aren't needed to
exercise it), an escaped backslash, a cascade-prevention case (escaped quote
followed by more comma-separated concatenation members on the same statement,
mirroring `base/uvm_root.svh:600`), and a negative control confirming a *truly*
unterminated string still errors (guards against an over-permissive fix that
accidentally consumes to EOF). Full unit suite: **895 assertions / 353 test cases**,
all green (up from 891/349 baseline — exactly the 4 new cases, zero regressions).

**Real-world verification**: rebuilt `release`, relinked the `probe_diag.cpp`-style
standalone probe (recreated per this section's description — the scratchpad copy
didn't survive between turns, as expected), reran the full UVM corpus. **Total
diagnostics dropped from 2956 → 1796 (−1160, −39%)**, files-with-diagnostics 73 → 72
(most affected files still have *some* remaining diagnostics from the other, still-
unfixed root causes, so this count barely moved even though per-file severity did).
Per-file drops confirm the fix is doing exactly what was predicted: `reg/
uvm_vreg.svh` 330 → 14, `reg/uvm_reg.svh` 151 → 77, `reg/sequences/
uvm_reg_mem_shared_access_seq.svh` 201 → 90, `base/uvm_root.svh` 67 → 27,
`base/uvm_component.svh` 26 → 2, `reg/uvm_vreg_field.svh` 115 → 15, `reg/
uvm_reg_field.svh` 92 → 55. Remaining diagnostics in these same files are exactly
the two other root causes identified above, untouched as expected — e.g.
`uvm_vreg.svh:74`/`:354` still show the Gap C stringification pattern
(`` `uvm_abstract_object_registry#(uvm_vreg_cbs,` `` `` ` `` ``), and `uvm_vreg.svh:457/
548/557` still show the `void'(...)` cast gap (`extraneous input ''' expecting '('`).
**Committed** at the user's explicit request: `b5cda7c` (fix + unit tests) and
`4e4fe5d` (this handoff section, at the time — since amended by later edits in
this same file for the Gap C work below, still uncommitted as of this writing).

### Gap C stringification — FIXED (2026-08-21, later same session, uncommitted)

Per explicit user request ("continue on gap c"), following up on root cause 1
above. Implemented the "stringification only, not token-pasting" scope suggested
by the original next-steps list.

**`src/compiler/sv_preprocessor.cpp`**: new static helper `tryStringify(src, i,
macros, errors, depth)`, called from `expandStr`'s backtick-dispatch loop whenever
`` ` `` is immediately followed by `"`. Scans forward for the next `` `" `` marker
pair (not nested — SV stringification spans don't nest), recursively
`expandStr`s the text between the two markers (so a stringified macro parameter —
already substituted to plain text by `substituteParams` before `expandStr` ever
runs — or a genuine nested macro invocation inside the span both resolve
correctly before quoting), backslash-escapes any `"`/`\` in the result so it forms
a syntactically valid string literal, and wraps it in real double quotes. An
unterminated `` `" `` (no matching close) records an error and stops, rather than
scanning to EOF. Existing column-shift bookkeeping (`colShifts`, used for
mid-line macro column-drift translation) is extended to also cover stringification
spans, using the same "record a breakpoint after the replacement text" pattern as
the macro-invocation branch right below it. Token-pasting (` `` `) is still not
recognized or supported anywhere — `src/compiler/sv_preprocessor.h`'s class doc
comment updated to reflect the new, narrower scope boundary.

**Unit tests** (`tests/unit/compiler/test_sv_preprocessor.cpp`, tag `[stringify]`,
4 new cases): a basic parameterized stringification; the *exact* real UVM
`` `uvm_type_name_decl ``/`` `m_uvm_object_registry_internal `` pattern from root
cause 1 (`` `define uvm_type_name_decl(TNAME_STRING) ... return `"TNAME_STRING`";
... `` invoked with a real class name, asserting the expected `return
"uvm_reg_err_service";` text appears in the output); a case proving the
stringification span's *contents* are macro-expanded before quoting (not just
copied literally); and an unterminated-marker negative control. Full unit suite:
**902 assertions / 357 test cases**, all green (up from 895/353 — exactly the 4
new cases, zero regressions).

**Real-world verification**: rebuilt `release`, relinked the standalone probe,
reran the full UVM corpus. **Total diagnostics dropped from 1796 → 1401
(−395, −22% further; −1555, −53% cumulative from the session's original 2956)**,
files-with-diagnostics 72 → 62. `` `uvm_object_utils ``/`` `uvm_component_utils ``
sites (root cause 1's primary target) are now confirmed fixed at the source —
e.g. `reg/uvm_reg.svh:52`'s `` `uvm_object_utils(uvm_reg_err_service) `` no longer
appears anywhere in that file's remaining diagnostics.

**What's left, root-caused**: re-inspected the largest remaining offenders
(`base/uvm_resource_pool.svh`, still 181 — completely unchanged by this fix; `reg/
uvm_vreg.svh`, 14 remaining, down from 330). Both point to **token-pasting**
(` `` `, still unsupported by design), and — importantly — this reveals
token-pasting's *own* real-world impact was also underestimated by session 1's
original "low impact" assessment (which only cited `` `uvm_copier_get_function ``/
`` `uvm_packer::get_packed_``T``s ``). Two more, distinct, common macro families
use it:
- `` `uvm_register_cb(T,CB) `` (`macros/uvm_callback_defines.svh:71-72`):
  `` static local bit m_register_cb_``CB = uvm_callbacks#(T,CB)::m_register_pair(
  `"T`",`"CB`"); `` — note this macro uses **both** token-pasting (`` m_register_cb_
  ``CB ``) **and** stringification (`` `"T`",`"CB`" ``) in the same expansion; the
  stringification half is now fixed (confirmed: `reg/uvm_vreg.svh:74`'s diagnostic
  changed from `` mismatched input '`' expecting IDENTIFIER `` to `` mismatched
  input '"uvm_vreg_cbs"' expecting IDENTIFIER `` post-fix — the quoted string is
  now syntactically correct, but the surrounding parse is still wrecked by the
  *earlier*, still-broken token-pasted prefix). This is also the exact macro
  behind the symbol-table-pollution bug found earlier this session
  (`uvm_report_catcher.svh:71`'s `` `uvm_register_cb(uvm_report_object,
  uvm_report_catcher) `` call) — so fixing token-pasting would likely also fix
  that bug at the source, as speculated there.
- `` `M__TABLE_Q(QUEUE_NAME) ``/`` `M__TABLE_GET(QUEUE_NAME, ITER) ``
  (`base/uvm_resource_pool.svh:136-143`, a locally-`` `define ``d, file-private
  macro pair, not a shared UVM macro): `` QUEUE_NAME``.value ``/`` QUEUE_NAME``
  .get(ITER) `` — confirms this file's unchanged 181 diagnostics (the single
  largest remaining offender) are *entirely* a token-pasting artifact, unrelated
  to stringification.

Also still present, unrelated to Gap C entirely: the already-documented
`void'(...)` cast gap (e.g. `reg/uvm_vreg.svh:457/548/557`, unchanged by either
fix this session) — see "`void'(...)` cast gap — FIXED" below, fixed shortly
after this section was originally written.

**Not implementing full token-pasting this session** — it's a materially bigger
feature than stringification was: stringification is a spans-in/string-out
substitution that fits the existing `expandStr` architecture directly, but
token-pasting needs identifier-*fragment*-level concatenation (`` A``B `` glues
two adjacent token fragments into one new identifier, potentially spanning
multiple macro-expansion boundaries) — a different, harder problem, and exactly
the boundary `SvPreprocessor`'s original design doc already called out
("use a slang-backed implementation for UVM-heavy codebases" for this specific
case). Flagging as a real, now better-quantified candidate for a future session
if the user decides it's worth the larger effort, rather than attempting it
without an explicit decision to do so.

### `void'(...)` cast gap — FIXED (2026-08-21, later same session)

Per explicit user request ("fix the void'(...) cast gap next"), following up on
the already-documented (pre-session-3) grammar quirk confirmed still pervasive in
real UVM by this session's measurements (e.g. `base/uvm_root.svh:916/941/945`,
`reg/uvm_vreg.svh:457/548/557`).

**`grammar/Sv.g4`**: `subroutine_call_statement`'s void-cast alternative
```
- | 'void' '(' subroutine_call ')' ';'
+ | 'void' SINGLE_QUOTE? '(' subroutine_call ')' ';'
```
The LRM (1800-2017 A.6.9) specifies `void ' ( function_subroutine_call ) ;` — this
grammar had dropped the apostrophe entirely, so only the non-standard `void(f())`
workaround form (used by `examples/dpi.sv`, with a comment explaining why)
parsed. Made the `SINGLE_QUOTE` *optional* rather than replacing it outright, so
both the LRM-correct `void'(f())` form used throughout real UVM and the existing
workaround form (still used by `examples/dpi.sv`) parse — no fixture needed to
change.

**Unit tests** (`tests/unit/compiler/test_sv_parser.cpp`, tag `[voidcast]`, 2 new
cases): the LRM-correct `void'(f())` form, and a regression guard confirming the
`void(f())` workaround form still parses (guards against the `SINGLE_QUOTE?` fix
accidentally becoming `SINGLE_QUOTE` required). Full unit suite: **904 assertions
/ 359 test cases**, all green (up from 902/357 — exactly the 2 new cases, zero
regressions).

**Real-world verification**: rebuilt `release`, relinked the standalone probe,
reran the full UVM corpus. **Total diagnostics dropped from 1401 → 1254
(−147, −10.5% further; −1702, −57.6% cumulative from the session's original
2956)**, files-with-diagnostics 62 → 51. Confirmed at the
per-file level: `reg/uvm_vreg.svh` 14 → 5 (the 3 `void'(...)` diagnostics at
lines 457/548/557 are gone; the remaining 5 are entirely the unrelated,
still-unfixed `` `uvm_register_cb `` token-pasting cascade at line 74), `base/
uvm_root.svh` 27 → 19, `base/uvm_callback.svh` 75 → 61.

**Committed**: `88f1610` (grammar + unit tests), `9cfd7a8` (this file's
grammar-quirks-table entry, committed before this fuller write-up and its
measured numbers were added).

### Session 3 cumulative result

Three fixes this session (`STRING_LITERAL` escapes, Gap C stringification,
`void'(...)` casts), each real-world-verified against the same 140-file UVM
corpus: **2956 → 1796 → 1401 → 1254 total diagnostics (−57.6% cumulative)**,
files-with-diagnostics **73 → 51**. All three are pure grammar/lexer/preprocessor
correctness fixes with no design ambiguity — the remaining diagnostics are now
concentrated almost entirely in token-pasting cascades (` `` `, deliberately
unsupported, see "Not implementing full token-pasting this session" above) plus
whatever Gap E (`` `ifdef `` inside `` `define `` bodies, not yet investigated this
session) turns out to be responsible for.

### Not yet done — suggested next steps

1. ~~Fix the `STRING_LITERAL` escape-sequence lexer gap~~ — **done**, committed
   (`b5cda7c`).
2. ~~Fix Gap C stringification~~ — **done**, committed (`5a41322`/`aca4eb1`).
3. ~~Fix the `void'(...)` cast gap~~ — **done**, committed (`88f1610`/`9cfd7a8`).
4. **Decide on full token-pasting support** — now confirmed to be the dominant
   remaining real-world diagnostic source (three distinct macro families found
   this session: `` `uvm_register_cb ``, `` `M__TABLE_Q ``/`` `M__TABLE_GET ``, plus
   the original `` `uvm_copier_get_function ``/`` `uvm_packer::get_packed_``T``s ``
   sites) but a materially larger implementation effort than stringification was
   — see "Not implementing full token-pasting this session" above for why. This is
   the natural next step if UVM-corpus diagnostic count keeps being a priority,
   but is a real design decision, not a quick follow-on fix.
5. Gap E (`` `ifdef `` inside `` `define `` bodies) still not investigated further
   this session — re-run the full-corpus probe after any of the above to see how
   much of the remaining ~1254 it's actually responsible for before deciding if
   it's worth pursuing on its own.
6. Consider the `findSymbolsByName`/hover disambiguation improvement noted in the
   symbol-pollution bug section — lower priority than a token-pasting fix, since
   fixing token-pasting would remove the garbage symbol at its source (see above)
   and would likely resolve that specific instance on its own, but the general "no
   kind preference, alphabetical tiebreak" pattern could still bite in unrelated
   future name collisions.
7. Consider whether the LSP diagnostics-visibility gap (primary-file-only
   `publishDiagnostics`) is worth addressing as its own small feature — e.g.
   proactively publishing diagnostics for every file touched by a `compile()` call,
   not just the primary one — independent of Phase 6.4's larger cross-file
   invalidation work, since it's a real, immediately-actionable editor-UX gap.

---

## UVM real-world smoke test — session 2 (2026-08-20, paused mid-session)

Continuation of the "UVM real-world smoke test" section further down this doc
(2026-07-20). That session left off with Gap A found-but-not-fixed and Gap B fixed.
This session's goal (explicit user request): "retry running uvm code. when it is all
in the database, create tests that tries to find random functionality from deep down
in uvm in order to see if there are more gaps."

### Gap A — FIXED (commit `9b8abed`)

`parseInvokeArgs` (`src/compiler/sv_preprocessor.cpp`) now tracks `{}`/`[]` nesting
depth alongside `()`, and is string-literal aware (a `"..."` argument containing a
comma or an unmatched paren — very common in real UVM message strings like
`` `uvm_warning(ID, {"...", behavior}) `` or `` `uvm_error("...(see above)...", x) `` —
no longer perturbs argument splitting or paren-depth tracking). 4 new unit tests,
tag `[bracenesting]`.

### Gap D (new) — multi-line macro invocations without backslash — FIXED (commit `5ba19f9`)

Real UVM (`base/uvm_phase.svh:1619-1624`, `` `UVM_PH_TRACE(...) `` split across two
lines with no trailing `\`) showed this doesn't parse: SV macro invocations, unlike
`` `define `` bodies, may split their argument list across physical lines using only
an open paren/brace/bracket — no LRM-mandated backslash continuation needed. The
line-by-line preprocessor only ever fed one physical line to `expandStr`, so such
invocations produced "missing required argument" errors and the trailing lines fell
through as unexpanded raw text (a downstream parse error).

**Fix:** `hasUnterminatedInvocation(line, macros)` (new,
`src/compiler/sv_preprocessor.cpp`) scans a line for invocations of *already-known*
function-like macros and reports whether the invocation's arg-list depth (same
paren/brace/bracket/string-aware counter as `parseInvokeArgs`) is still open at end
of line. `mergeInvocationContinuation` (new, a lambda inside `processSource`) pulls
in further physical lines via the same `std::getline` stream until it balances,
advancing `lineNo` as it goes. **Ordering subtlety that cost real debugging time:**
the merged invocation's *real* (expanded) content must be emitted to `ctx.output`
**before** the blank lines for the continuation lines it consumed — not after — both
because `ctx.output` is parsed by ANTLR sequentially (wrong order = tokens land on
the wrong output line entirely) and because the sourceMap entry for the real content
must say the *first* line, not whatever `lineNo` had advanced to by the time it's
emitted. `emitLine`/`emitBlank` both gained an optional explicit `atLine` parameter
for this reason (default `-1` = "use current `lineNo`", unchanged for every existing
call site). 5 new unit tests, tag `[multiinvoke]`. Known accepted imprecision: any
token that lands specifically on a continuation line (not the first) is still
attributed to the first line for diagnostics/hover — matches the existing precedent
for multi-line `` `define `` bodies.

**Verification:** full unit suite 349 cases/891 assertions (debug/ASan), zero
regressions from 340/868 baseline at session start.

### Standalone UVM corpus re-run after both fixes

Using the same standalone-probe technique from session 1 (recreate `probe.cpp` per
that section's notes if missing — this session's copies all lived in the session
scratchpad, not committed, paths won't survive to a new session):
preprocessor-only errors on the real UVM corpus (`uvm.sv`, `includeDirs: ["."]`,
`UVM_NO_DPI` defined, release build) dropped **335 → 173 → 86**: Gap A's fix alone
resolved 162 (the brace-nesting arity-mismatch cluster); Gap D's fix resolved the
remaining 87 "missing required argument" cases (arity=0 after both fixes). The
remaining 86 are **all** "undefined macro" errors, in two known clusters — no third
cluster:

- **Gap C (already known, deliberately unsupported)** — token-pasting (` `` `).
  `` `uvm_copier_get_function(FUNCTION) `` (`macros/uvm_copier_defines.svh`) expands
  `` get_``FUNCTION``_copy `` and `` uvm_packer::get_packed_``T``s ``
  (`base/uvm_packer.svh`) — both leave literal `` `` `` in the output that then gets
  mis-scanned as more macro invocations (`` `first ``, `` `_copy ``, `` `byte ``,
  `` `s ``, etc. — matches the FUNCTION/T argument names actually used at each call
  site). Confirmed low real-world impact *on uvm-core itself* (not necessarily on
  end-user testbenches, which use field-automation macros far more): only 17 direct
  `` `uvm_field_* `` invocations and 43 files referencing any `` `uvm_*_utils ``
  macro across the whole ~170-file corpus.

- **Gap E (new) — `` `ifdef ``/`` `else ``/`` `endif `` embedded inside a `` `define ``
  body are not evaluated at macro-expansion time.** Real UVM
  (`macros/uvm_object_defines.svh:826-831`):
  ```
  `define m_uvm_field_op_begin(OP, FLAG) \
  UVM_``OP: \
    if ( \
       `ifndef UVM_LEGACY_FIELD_MACRO_SEMANTICS (((FLAG)&UVM_``OP)) && `endif \
       (!((FLAG)&UVM_NO``OP)) \
    ) begin
  ```
  and `macros/uvm_object_defines.svh:797-808` (`` `m_warn_if_no_positive_ops ``,
  `` `ifdef UVM_LEGACY_FIELD_MACRO_SEMANTICS ... `else ... `endif `` inside the body).
  When such a macro is invoked, `expandStr` recursively expands the body text but has
  no concept of conditional directives inside it — `` `ifdef ``/`` `else ``/`` `endif ``
  are scanned like any other `` ` `` + identifier and hit `expandMacroCall`'s
  "undefined macro" path (they're not in the macro table), which just emits `""` for
  each and moves on — meaning **both** branches' literal text end up concatenated
  into the output, un-conditioned. **Fix sketch (not attempted — substantial,
  needs design):** when parsing/expanding a macro body, recognize
  `` `ifdef ``/`` `ifndef ``/`` `elsif ``/`` `else ``/`` `endif `` tokens and
  re-run the same conditional-compilation logic used at the top level, evaluated
  against the *current* (invocation-time) `ctx.macros` state — not definition-time,
  since e.g. `UVM_LEGACY_FIELD_MACRO_SEMANTICS` could be defined by the invoking
  file but not by whatever file originally `` `define ``d the macro. This is a
  distinctly separate design problem from Gap C (token-pasting) even though both
  currently manifest as "undefined macro `ifdef`/`else`/`endif`/`COPY`/`COMPARE`/
  `PACK`/`UNPACK`" in the error list — the `COPY`/`COMPARE`/`PACK`/`UNPACK` names
  are `` `m_uvm_field_op_begin ``'s `OP` argument at each of its real call sites,
  confirming it's this macro, not a different one. Same low-real-impact caveat as
  Gap C applies (only reachable via the same 17 field-automation-macro call sites
  inside uvm-core itself).

### Major finding (new): ANTLR parse performance at real-world scale — NOT FIXED, Phase 6.5 territory

**This is the most significant discovery of this session** and the reason the
"get UVM fully into the DB" part of the user's request is not yet complete.

**Symptom:** opening the real `uvm.sv` (full `` `include `` chain, ~170 files,
~85K preprocessed lines) through the actual `svlsp` binary via a JSON-RPC driver
(`didOpen` → wait for `publishDiagnostics`) did not return within several minutes
(observed: still running, 100% CPU, RSS climbing steadily — 727MB+ — past 6 minutes
of wall time before being killed to investigate). This is **not an infinite loop**:
RSS grows roughly linearly, not explosively, and standalone measurements below
confirm it eventually terminates, just very slowly.

**Isolation technique (extends the session-1 standalone-probe method):** rather than
going through the full LSP/DB layer, link a tiny driver directly against
`build/release/libsvlsp_compiler.a` + `libsvlsp_antlr4.a` +
`_deps/antlr4_runtime-build/runtime/libantlr4-runtime.a` (include paths:
`-I src -I build/release/generated/antlr4
-I build/release/_deps/antlr4_runtime-src/runtime/Cpp/runtime/src`, and
**`-pthread` is required** — omitting it causes an immediate
`std::system_error: Unknown error -1` crash inside the ANTLR runtime's static
initialization, which looks alarming but is just a missing link flag, not a real
bug) and call `SvTreeWalker::walk` directly, timing it separately from
`SvPreprocessor::process`. None of these probes were committed — recreate from this
description if needed for a future session.

**Measurements (release build, no ASan):**
- Full corpus preprocessing alone: ~0.08-0.2s (fast, as in session 1 — the
  preprocessor itself was never the bottleneck).
- `base/uvm_barrier.svh` alone (236 lines, standalone/no macro context so
  `` `uvm_object_utils `` is "undefined" → 3 real parse errors from the resulting
  malformed class-body text): **2.45-2.79s** to parse. Forcing ANTLR's SLL-only
  prediction mode (`parser.getInterpreter<antlr4::atn::ParserATNSimulator>()->
  setPredictionMode(antlr4::atn::PredictionMode::SLL)`, tested standalone, **not**
  applied to product code) brought this to 2.13s (~20% faster) — a real but
  partial improvement; the dominant cost is ANTLR's error-recovery/resynchronization
  machinery itself, not full-context (SLL→LL fallback) prediction.
- `base/uvm_base.svh` (150-line aggregator, `` `include ``s ~50 files under `base/`,
  tested standalone so its 144 "undefined macro" errors are mostly the same
  missing-macro-context artifact as above, not real Gap C/E errors): **334.578s
  (~5.6 minutes)** to parse.
- A **bounded, properly macro-primed** 5-real-file subset was built to separate
  "slow because of preprocessor-error-driven parse errors" from "slow because large
  real SV class bodies are just inherently expensive to parse": a synthetic top file
  `` `include ``ing `uvm_macros.svh` first (so `` `uvm_info ``/`` `uvm_object_utils ``/
  etc. are genuinely defined, unlike the standalone-file tests above) then
  `base/uvm_object.svh`, `base/uvm_component.svh`, `seq/uvm_sequence_item.svh`,
  `reg/uvm_reg.svh`, `tlm1/uvm_analysis_port.svh` directly (these 5 files have zero
  or one `` `include `` of their own — chosen specifically to avoid the aggregator
  fan-out). **Even with proper macro context, this timed out past 60s** — i.e. the
  slowness is **not solely** a Gap C/E artifact; large real UVM class bodies
  (`uvm_component.svh` is 3780 lines) are independently, inherently slow to parse
  under this grammar. Per-file standalone timings (missing-macro-context artifact
  errors present, so treat as upper bounds, not clean numbers): `uvm_object.svh`
  (1325 lines, 3 pp errors) 8.3s; `uvm_sequence_item.svh` (568 lines, 1 pp error)
  9.85s; `tlm1/uvm_analysis_port.svh` (175 lines, 4 pp errors) 1.7s;
  `uvm_component.svh` (3780 lines, 36 pp errors) and `reg/uvm_reg.svh` (3060 lines,
  41 pp errors) both exceeded 30s without finishing.
- **Conclusion:** two compounding effects, not one — (1) a genuine, inherent
  per-line ANTLR parsing cost on real (correctly-preprocessed) SV that's roughly
  linear but with a high constant factor (order 10-20ms/line extrapolated from the
  clean small-file numbers above), and (2) a much larger, super-linear penalty
  (multiple seconds *per site*) wherever a Gap C/E preprocessor error leaves
  malformed text, driven by ANTLR's error-recovery/resynchronization cost on this
  3828-line grammar. Both are real; (2) is avoidable by fixing Gap C/E, (1) is not
  without deeper ANTLR/grammar performance work (Phase 6.5 territory: profiling,
  possibly grammar restructuring to reduce ambiguity, possibly a different parsing
  strategy for hot paths). **Not attempted this session.**

### Not yet done — exactly where to resume

The user's request has two parts; only the fix/investigation part above is done.
Still outstanding:

1. **Get the full real UVM corpus into the DB via the actual `svlsp` LSP server**
   (not just the standalone preprocessor/parser probes above) — i.e. actually run
   `build/release/svlsp`, `didOpen` on `uvm.sv` (project manifest already exists at
   `/home/martin/src/verilator_test/uvm-core/src/.svlsp.json`, recreate if missing:
   `{"files": ["uvm.sv"], "includeDirs": ["."], "defines": {"UVM_NO_DPI": ""}}`), and
   let it run to completion. Given the performance finding above, budget **at least
   10-20 minutes** of wall time for this, run it as a true background process (not
   inside a single tool-call timeout), and expect it to eventually succeed (not
   hang forever) based on the standalone measurements. A JSON-RPC driver script for
   this was written this session at (session scratchpad, not committed, recreate
   from scratch — straightforward stdio Content-Length framing, see session 1's
   "Debugging technique notes" for the pattern) `lsp_driver.py`: spawns
   `build/release/svlsp`, does `initialize`/`initialized` with `rootUri` pointing at
   the UVM src dir, `didOpen`s `uvm.sv` with its own text (the server resolves
   `` `include ``s from disk itself, no need to preload them client-side), and waits
   for `publishDiagnostics`.
2. **As a faster near-term alternative** (while the full corpus run is pending, or
   instead of it if 10-20 minutes is judged not worth it for exploratory testing):
   use the bounded 5-real-file subset described above (`uvm_macros.svh` +
   `uvm_object.svh` + `uvm_component.svh` + `uvm_sequence_item.svh` + `uvm_reg.svh`
   + `uvm_analysis_port.svh`) as the corpus opened through the real LSP instead —
   still genuine, unmodified, deep UVM source spanning `base/`, `seq/`, `reg/`,
   `tlm1/`, just without the full ~170-file fan-out. Note this subset alone was
   *also* slow in the standalone ANTLR-only probe (>60s, see above) — confirm it
   actually finishes before relying on it, budget a few minutes.
3. **Write exploratory hover/definition/documentSymbol/workspace-symbol queries**
   against real, deep UVM symbols once whichever corpus above is loaded, to look for
   *further* LSP-layer gaps (not preprocessor-layer — those are covered above)
   beyond what's already known. Candidate symbols already located this session
   (grep `^\s*(virtual\s+)?class\s+NAME\b` across the corpus for exact
   file:line — re-run if the corpus changes):
   - `uvm_component` → `base/uvm_component.svh:59`
   - `uvm_object` → `base/uvm_object.svh:61`
   - `uvm_root` → `base/uvm_root.svh:98`
   - `uvm_phase` → `base/uvm_phase.svh:147`
   - `uvm_objection` → `base/uvm_objection.svh:79`
   - `uvm_report_server` → `base/uvm_report_server.svh:65`
   - `uvm_resource_db` → `base/uvm_resource_db.svh:66`
   - `uvm_config_db` → `base/uvm_config_db.svh:58`
   - `uvm_event` → `base/uvm_event.svh:282`
   - `uvm_barrier` → `base/uvm_barrier.svh:45`
   - `uvm_heartbeat` → `base/uvm_heartbeat.svh:67`
   - `uvm_coreservice_t` → `base/uvm_coreservice.svh:71`
   - `uvm_domain` → `base/uvm_domain.svh:78`
   - `uvm_agent` → `comps/uvm_agent.svh:51`
   - `uvm_driver` → `comps/uvm_driver.svh:58`
   - `uvm_monitor` → `comps/uvm_monitor.svh:45`
   - `uvm_scoreboard` → `comps/uvm_scoreboard.svh:47`
   - `uvm_algorithmic_comparator` → `comps/uvm_algorithmic_comparator.svh:81`
   - `uvm_sequence` → `seq/uvm_sequence.svh:47`
   - `uvm_sequence_item` → `seq/uvm_sequence_item.svh:52`
   - `uvm_sequencer` → `seq/uvm_sequencer.svh:44`
   - `uvm_sequence_base` → `seq/uvm_sequence_base.svh:153`
   - `uvm_reg` → `reg/uvm_reg.svh:102`
   - `uvm_reg_field` → `reg/uvm_reg_field.svh:50`
   - `uvm_mem` → `reg/uvm_mem.svh:57`
   - `uvm_reg_block` → `reg/uvm_reg_block.svh:40`
   - `uvm_analysis_port` → `tlm1/uvm_analysis_port.svh:68`
   - `uvm_tlm_generic_payload` → `tlm2/uvm_tlm2_generic_payload.svh:114`

   Plan: `workspace/symbol` for each name (verify resolved URI matches expected
   file); `documentSymbol` by path on a handful of the files above (works without
   opening them — per Phase 6.1, `documentSymbol`/`workspace/symbol` query the DB
   directly, no `m_store.contains` check, unlike hover/definition); for
   hover/definition specifically (which *do* require the doc open via
   `m_store.contains`), `didOpen` 2-3 of the files above directly and test hover/
   definition on a cross-file base-class or type reference inside them.
4. Given the corpus is real, external, and not tracked by svlsp's own git (per
   session 1's note), any test built from this **should stay a scratchpad/manual
   exploration tool**, not a committed `tests/integration/*.sh` — consistent with
   how session 1 handled this same tension.
5. Update this section (or add a new dated one) with whatever the exploratory
   queries find, once run.

---

## UVM real-world smoke test (2026-07-20, in progress)

**Goal:** the user asked to set up a project compiling the real UVM core library
(`../verilator_test/uvm-core/src/uvm.sv`, a sibling checkout **outside** this repo at
`/home/martin/src/verilator_test/`, not tracked by svlsp's git) with
`includeDirs: ["."]` and `defines: { UVM_NO_DPI: "" }`, and see whether it compiles —
a first real-world stress test of the preprocessor/parser beyond hand-written fixtures.

**Project manifest created** (outside this repo, not committed anywhere — recreate if
missing) at `/home/martin/src/verilator_test/uvm-core/src/.svlsp.json`:
```json
{
    "files": ["uvm.sv"],
    "includeDirs": ["."],
    "defines": { "UVM_NO_DPI": "" }
}
```
Placed *alongside* `uvm.sv` so `ProjectRegistry`'s upward search finds it immediately
when that file is opened. `uvm.sv` itself is a 35-line wrapper that `` `include ``s
`uvm_pkg.sv`, which transitively `` `include ``s ~170 files under `uvm-core/src/`.

### Debugging technique notes (reusable for future investigations)

- **`ptrace` is blocked in this sandbox** — `gdb -p <pid>` fails with "Operation not
  permitted" even for your own process. Don't waste time on attach-based debugging
  here; instead copy the suspect `.cpp` to scratch, add `std::cerr` progress prints
  (e.g. one per `` `include `` enter/exit, one every N lines of a loop), and compile
  it standalone.
- **Isolating the preprocessor from the full LSP/ANTLR stack is fast and cheap.**
  `SvPreprocessor` has no ANTLR dependency (only `SvTreeWalker` does), so a standalone
  probe that only calls `CompilerDirectiveStripper::strip` + `SvPreprocessor::process`
  compiles and links in seconds against the already-built static lib:
  `g++-13 -std=c++20 -O2 -I src probe.cpp build/release/libsvlsp_compiler.a -o probe`
  (no antlr4 headers/objects get pulled in since `sv_tree_walker.o` is never
  referenced). This is what actually found both bugs below — going through the full
  JSON-RPC/LSP layer first only wastes time confirming "it's slow/hung" without
  saying where.
- **Use the `release` CMake preset for anything performance-sensitive.** The default
  `debug` preset has ASan+UBSan instrumentation; both hung indefinitely on real UVM
  before the fixes below, and `release` was needed to get fast (~0.1s) iteration once
  actually measuring rather than debugging a hang.
- A throwaway Python JSON-RPC driver script (spawn `build/release/svlsp`, send
  `initialize`/`initialized`/`didOpen`, read the `publishDiagnostics` notification with
  a `Content-Length`-framed reader) was used for the one true end-to-end check: it
  lived in the session scratchpad only, not committed — recreate if needed, it's
  ~70 lines, straightforward stdio JSON-RPC framing matching the smoke-test example
  already in this doc's "Build and test" section.

### Bug 1 — infinite loop on function-like macro default argument values — FIXED, uncommitted

SV/Verilog macro parameter lists may give a parameter a default value
(`` `define M(A, B=expr) ``), used when the invocation omits that trailing argument.
UVM's `uvm_report_begin` macro (`uvm-core/src/macros/uvm_message_defines.svh`) uses
exactly this: `` `define uvm_report_begin(SEVERITY, ID, VERBOSITY,
RO=uvm_get_report_object()) \ ... ``. `parseMacroDefinition`'s function-like
parameter-list loop (`src/compiler/sv_preprocessor.cpp`) read the parameter name via
`readIdent`, then only checked for a following `,` before looping back — on hitting
`=` it recognized neither `,` nor `)`, so the `while` condition stayed true forever
without `i` ever advancing: **a true infinite loop**, not just slow. This is what was
actually hanging both the debug *and* release builds indefinitely on `uvm.sv` (a 35
line top file — the hang has nothing to do with file size or ASan overhead, contrary
to the initial assumption while debugging).

**Minimal repro** (still useful as a regression check if the unit tests below are
ever deleted): `` `define FOO(A, B=default_expr()) A B `` then invoke `` `FOO(1) ``.

**Fix:**
- `MacroDef` (`src/compiler/sv_preprocessor.cpp`) gained
  `std::vector<std::optional<std::string>> defaults;`, parallel to `params`.
- The parameter-list loop now: (a) if `readIdent` returns empty at a position where a
  param name was expected, pushes an error and `break`s instead of looping forever —
  a general safety net against any future malformed-list hang, not just this one
  pattern; (b) after reading a param name, checks for `=` and if found scans the
  default-value expression up to the next **top-level** `,` or `)`, tracking paren
  depth so a default value that is itself a call (`uvm_get_report_object()`) doesn't
  end the scan early at its own closing paren.
- `expandMacroCall`: arity check changed from `args.size() != params.size()` to
  `args.size() > params.size()` (too many is still an error), then for every
  param index beyond the supplied args, uses `def.defaults[k]` if present, else
  errors `"missing required argument"`.
- The other `MacroDef` construction site (predefined macros via `SvPreprocessor::
  define()`) used positional aggregate init `MacroDef{false, {}, value}` assuming
  3 fields — updated to designated-init `MacroDef{.isFunctionLike = false, .body =
  value}` now that there are 4 fields.
- Unit tests added (`tests/unit/compiler/test_sv_preprocessor.cpp`, tag
  `[defaultargs]`, 5 cases): default used when omitted, default overridden when
  supplied, default value containing nested parens doesn't hang (direct regression
  test for the UVM pattern), missing required non-default arg still errors, too many
  args still errors.

### Bug 2 — `//` comments scanned for macro invocations — FIXED, uncommitted

UVM's source is heavily doc-commented with examples like
`` // |`uvm_info(ID, MSG, VERBOSITY) `` (literal backtick-macro syntax shown inside a
`//` comment, `uvm-core/src/macros/uvm_message_defines.svh` and many other files).
`expandStr` (`src/compiler/sv_preprocessor.cpp`) scanned the **entire raw line** for
`` ` `` characters with no awareness of `//` comments at all, so it tried to expand
`` `uvm_info ``, `` `ifdef ``, `` `define ``, `` `endif `` etc. found inside comment
text as if they were real invocations — producing ~1780 spurious "undefined macro"
errors on the UVM corpus alone (confirmed via `grep -rn '//.*`ifdef\|//.*`define
\|//.*`uvm_info' uvm-core/src/` before fixing, which found many matches).

**Fix:** new helper `splitLineComment(line)` (naive `find("//")`, mirrors the
existing `stripLineComment` used for directive-argument lines) splits a regular
source line into a macro-expandable code portion and a literal trailing comment
portion. `processSource`'s regular-line branch now only calls `expandStr` on the code
portion and appends the comment portion verbatim afterward — column-shift tracking
(the mid-line-macro fix from earlier this session) is unaffected since it's computed
from the code-portion expansion only, and the comment text after it never contains
real symbols anyway.
Unit tests added (tag `[comments]`, 2 cases): backtick-like text inside a
comment-only line is not expanded and passes through unchanged; a real macro
invocation earlier on a line still expands correctly even when a comment
*containing another backtick-like token* follows on the same line.

### Verification (both fixes together)

- Unit suite: 337 cases / 861 assertions, all green.
- Full Emacs integration suite: 124 passed / 1 failed — the 1 failure is the
  pre-existing, already-documented `test_08_completion.sh` flake (confirmed
  unrelated by running it in isolation earlier this session); zero regressions.
- Real UVM (`uvm.sv` + full include chain, `UVM_NO_DPI` defined): preprocessing now
  completes in **~0.07-0.09s** (previously hung indefinitely, confirmed >5 minutes
  with no progress on both debug and release builds before the fix) — 85,787 output
  lines / ~2.8MB from ~170 recursively included files. **1,241 diagnostics remain**,
  breakdown: the large majority are the arity-mismatch pattern from Gap A below
  (confirmed by manually inspecting several `` `uvm_warning ``/`` `uvm_error ``
  call sites in `uvm_object_defines.svh` that pass a `{...}` concatenation
  expression as an argument); the remainder (17: 9× `` `__FILE__ ``, 8× `` `__LINE__ ``)
  are Gap B below.

### Remaining gaps found but NOT fixed (real, reproducible, not yet started)

**Gap A — macro-argument parsing doesn't track `{}`/`[]` nesting.**
`parseInvokeArgs` (`src/compiler/sv_preprocessor.cpp`) only tracks `(`/`)` depth when
deciding whether a `,` is a top-level argument separator or part of a nested
sub-expression. SystemVerilog concatenation expressions `{a, b}` and array/queue
literals are common macro-argument contents and use `{`/`}`, not `(`/`)`. Real
example (`uvm-core/src/macros/uvm_object_defines.svh:805` and similar):
`` `uvm_warning("UVM/FIELDS/NO_FLAG",{"Field macro for ARG uses FLAG without or'ing
any explicit UVM_xxx actions. ",behavior}) `` — the comma inside `{...}` is
incorrectly treated as ending the 2nd argument early, splitting it into 2 extra
arguments and producing `` macro `uvm_warning`: expected at most 2 args, got 4 ``.
**Fix sketch:** extend `parseInvokeArgs`'s existing `depth` counter (currently only
incremented/decremented on `(`/`)`) to also count `{`/`}` (and probably `[`/`]` for
consistency, though no real example of that surfaced yet) toward the same depth
value — a comma is only a real separator at depth == 1 measuring "inside the
invocation's outer parens, at no nested bracket of any kind". Add a unit test using
literally this pattern (2-param macro invoked with a 2nd argument that's a brace
expression containing a comma) as the regression case.

**Gap B — `` `__FILE__ ``/`` `__LINE__ `` unresolved inside `` `include ``d files —
FIXED, uncommitted (2026-07-20).**
The two-pass pipeline is: pass 1 (`CompilerDirectiveStripper::strip`) substitutes
`` `__FILE__ ``/`` `__LINE__ `` with literals, then pass 2 (`SvPreprocessor::process`)
handles `` `include `` (among other things) by reading the included file's raw text
and recursing into `processSource` directly — it never ran pass 1 on included
files, only on the single top-level source handed to `SvPreprocessor::process` by
its caller (`CompilationController`, which itself calls `CompilerDirectiveStripper::
strip` once before calling `SvPreprocessor::process`). So any `` `__FILE__ ``/
`` `__LINE__ `` inside an included file reached `expandStr` as a literal, still-unresolved
macro invocation → "undefined macro" error. Confirmed via the UVM run (9×
`` `__FILE__ ``, 8× `` `__LINE__ ``, all presumably inside included `.svh` files, not
`uvm.sv` itself which has neither literal).

**Fix applied:** option (a) from the original sketch — `processInclude`
(`src/compiler/sv_preprocessor.cpp`) now runs `CompilerDirectiveStripper::
strip(includedText, foundPath)` on each included file's raw text before recursing
into `processSource`, passing `stripped.source` instead of the raw text. Line count
is preserved (stripped directives become blank lines, same as pass 1's existing
guarantee for the top-level file), so the preprocessor source map is unaffected.
`stripped.directives` is discarded — nothing currently consumes that field even at
the top level (`CompilationController::compile` only uses `.source`). No design
issue turned up in practice: pass 1 becoming "recursive" (once per file, called from
inside pass 2's include handling rather than only once up front by the external
caller) required no restructuring beyond the one call site — `CompilerDirectiveStripper::
strip` was already a pure function of `(source, filepath)` with no shared state.

**Verification:** new unit tests (`tests/unit/compiler/test_sv_preprocessor.cpp`,
3 cases): `` `__LINE__ `` inside an included file resolves to that file's own line
number; `` `__FILE__ `` inside an included file resolves to the included file's path
(not the top-level file's); a `` `timescale `` (pass-1 metadata directive) inside an
included file is stripped silently instead of falling through to pass 2. Full unit
suite: 340 cases / 868 assertions, all green (up from 337/861 before this fix — the
3 new cases). Manually re-confirmed against a standalone nested-include repro
(`__FILE__`/`__LINE__` used inside a macro body defined in an included file, invoked
from that same file) via the g++-13-against-`libsvlsp_compiler.a` probe technique
documented above: `` `__FILE__ `` now correctly resolves to the *included* file's
path, not the top-level file's. (Note: `` `__LINE__ `` used *inside a macro
definition* resolves to the line where the `` `define `` itself sits, not the
invocation site — this is pass 1 substituting literally wherever the token appears
in the raw source, before macro expansion even begins; that's pre-existing behavior
for top-level files too, not something this fix changed or introduced.)
Re-run against the real UVM corpus (`uvm.sv`, `includeDirs: ["."]`,
`UVM_NO_DPI` defined) via the standalone `libsvlsp_compiler.a` probe technique,
release build: preprocessing completes in ~0.16s, errors dropped from 1,241 to
**335** with **zero** `` `__FILE__ ``/`` `__LINE__ `` errors remaining (previously
9 + 8 = 17) — confirms the fix on the real corpus, not just the unit tests. The
remaining 335 are all Gap A (brace-nesting arity mismatches), still unfixed.

**Gap B end-to-end regression test (`test_23_include_file_line.sh`, 6 cases,
committed):** the unit tests above exercise `SvPreprocessor::process` directly;
this test proves the fix through the real JSON-RPC/LSP layer, matching this
codebase's "every LSP feature needs both a unit test and a functional Emacs test"
working rule. Fixtures `tests/integration/fixtures/gapb_top.sv` (`` `include ``s
`gapb_inc.sv`, defines `gapb_top_mod`) and `gapb_inc.sv` (`gapb_before_mod`,
then `gapb_marker_mod` whose parameter defaults use `` `__FILE__ ``/`` `__LINE__ ``,
then `gapb_after_mod` — the before/after modules straddle the marker so a
broken line count from pass-1 stripping would misplace `gapb_after_mod`).
Assertions: zero diagnostics after opening `gapb_top.sv` (the primary
regression check — before the fix this was a non-zero "undefined macro"
diagnostic); `documentSymbol` on `gapb_top.sv` has `gapb_top_mod` only, not
the included file's modules; `documentSymbol` on `gapb_inc.sv` (by path) has
all three modules at their correct original lines; `workspace/symbol` for
`gapb_marker_mod` points to `gapb_inc.sv`. All 6 pass; full integration suite
rerun clean (130 passed / 1 failed — the same pre-existing
`test_08_completion.sh` flake noted above, zero new regressions).

**Gap C — stringification (`` `" ``) and token-pasting (` ``` `) — NOT a bug, deliberately unsupported.**
UVM uses `` `"ARG`" `` (stringify) in several macros (e.g.
`uvm_object_defines.svh:1151` and elsewhere). `SvPreprocessor`'s own class doc
comment already states this scope boundary explicitly: "Stringification (`") and
token-pasting (``) are not supported in this implementation; use a slang-backed
implementation for UVM-heavy codebases." Not something to silently fix as a
byproduct of this investigation — flagging here only so a future full "does UVM
compile" attempt doesn't mistake it for a new discovery. Revisit only if/when a
slang-backed preprocessor replacement is undertaken.

### Multi-level `` `include `` integration test — Complete (2026-07-20)

The existing `test_15_preprocessor_lsp.sh` only covers **one level** of `` `include ``
(`preproc_main.sv` includes `preproc_defs.sv`, which defines one macro and one
module, no conditionals). The user asked for a new test covering **multiple levels**
of `` `include `` where preprocessor *statements* (not just plain symbols) appear in
*multiple* of the nested files — i.e. macro definitions and `` `ifdef `` conditionals
interacting across more than one include boundary, which nothing today exercises.
A `Write` call for the first fixture file was interrupted before any file existed —
below is the full design, ready to implement from scratch next session.

**Fixture layout** (3 levels, new files under `tests/integration/fixtures/`):
- `ml_top.sv` (level 1, opened directly by the test) — `` `define ML_TOP_WIDTH 8 ``
  *before* `` `include "ml_level2.sv" ``; after the include, a module using
  `` `ML_TOP_WIDTH `` (defined right there) **and** `` `ML_LEVEL3_DEPTH `` (defined
  two include-levels down, in `ml_level3.sv` — proves a macro survives back up to
  the top once the whole include chain unwinds); instantiates both
  `ml_level2_mod` (1 level down) and `ml_level3_mod` (2 levels down) so hover/
  definition can be tested at both distances from a single opened buffer.
- `ml_level2.sv` (level 2) — `` `define ML_LEVEL2_SCALE 2 ``, then
  `` `include "ml_level3.sv" ``, then `` `ifdef ML_LEVEL3_FLAG `` gating
  `module ml_level2_mod` — the flag is defined **inside** `ml_level3.sv`, so this
  proves an `` `ifdef `` in the *middle* file correctly sees a macro defined by the
  *deepest* file, textually inserted just above it by the nested include. Also add a
  **negative** `` `ifdef ML_NEVER_DEFINED `` guarding a `module ml_should_not_exist`
  that must never appear anywhere (in output, diagnostics, or any DB query) —
  without a negative case, a test could pass even if `` `ifdef `` were accidentally
  short-circuited to "always true".
- `ml_level3.sv` (level 3, deepest) — `` `define ML_LEVEL3_FLAG `` and
  `` `define ML_LEVEL3_DEPTH 4 ``, then `module ml_level3_mod` whose body uses
  `` `ML_TOP_WIDTH `` (defined in the *top* file, two include-levels *above* —
  proves downward visibility through more than one nested include, the mirror image
  of the upward case tested in `ml_top.sv`).

**New test file:** `tests/integration/test_24_multilevel_include.sh` (renumbered from
`test_23` — that slot was taken by the Gap-B regression test below), following the
exact `run_test`/`section`/guard-block conventions already used by
`test_15_preprocessor_lsp.sh` (read that file first — it's the closest existing
precedent, just extended from 1 include level to 3, and from "no conditionals" to
"`` `ifdef `` spanning include boundaries in both directions"). Planned assertions:
- Diagnostics: zero, after opening `ml_top.sv` (proves the whole 3-level chain with
  both cross-boundary macro directions and the ifdef compiles clean).
- Hover on the `ml_level2_mod` instantiation site in `ml_top.sv` → non-null.
- Hover on the `ml_level3_mod` instantiation site in `ml_top.sv` → non-null (proves
  cross-file resolution works at 2 levels of include distance, not just 1 — nothing
  existing tests this; `findSymbolsByName` is already global/unscoped per the Phase
  6.2 "key facts" note above so this is expected to already work with zero code
  changes, but it's untested).
- Definition on `ml_level2_mod` instantiation → resolves to `ml_level2.sv` at its
  correct original (pre-`` `include ``-expansion) line.
- Definition on `ml_level3_mod` instantiation → resolves to `ml_level3.sv` at its
  correct original line (2-level case).
- `documentSymbol` on `ml_top.sv` → contains `ml_top_mod`, does **not** contain
  `ml_level2_mod`/`ml_level3_mod`/`ml_should_not_exist`.
- `documentSymbol` on `ml_level2.sv` (queried by path, not opened as a buffer — same
  pattern as `test_15`'s `DEFS_FIXTURE` check) → contains `ml_level2_mod` at its
  correct original line, does **not** contain `ml_should_not_exist` (the negative
  `` `ifdef `` case).
- `documentSymbol` on `ml_level3.sv` (by path) → contains `ml_level3_mod` at its
  correct original line.
- `workspace/symbol` query for `ml_level3_mod` → location URI points to
  `ml_level3.sv`, not `ml_top.sv` or `ml_level2.sv` (proves correct file attribution
  survives 2 levels of include nesting through the source map, matching the
  single-level assertion `test_15` already makes for 1 level).
- `workspace/symbol` query for `ml_should_not_exist` → no match anywhere (negative
  `` `ifdef `` case, global check).
Exact original line numbers for each assertion were computed by reading the fixture
files back with the `Read` tool after writing them, per the note above.

**Implemented as designed, no changes to the design needed.** Fixtures
`tests/integration/fixtures/{ml_top,ml_level2,ml_level3}.sv` and
`tests/integration/test_24_multilevel_include.sh` (10 test cases, all from the
planned-assertions list above, none dropped or added). Verified line numbers and
zero preprocessor/parse errors first with a standalone probe (same
`libsvlsp_compiler.a` + `svlsp_antlr4` + `antlr4-runtime` static-link technique
noted above, extended to also call `SvTreeWalker::walk` and inspect `walked.records`
directly) before writing the Emacs test, to catch any grammar/fixture mistakes
without paying the Emacs-daemon round-trip cost — all 10 assertions then passed on
the first Emacs run with no fixture rework needed. Full integration suite rerun
clean: 140 passed / 1 failed (the same pre-existing `test_08_completion.sh` flake
noted throughout this doc), zero new regressions. Confirms (all with zero
production-code changes, exactly as predicted in the original design): `` `ifdef ``
in a middle include file correctly sees a macro defined by a deeper include;
macros defined in the deepest file remain visible after the whole chain unwinds
back to the top file; hover/definition/`workspace symbol` all resolve correctly at
2 levels of include distance, not just the 1 level `test_15` already covered; a
negative `` `ifdef `` guarding a never-defined macro correctly excludes its module
everywhere (diagnostics, `documentSymbol`, and `workspace/symbol`, not just one of
the three).

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
                   signature_help, symbol_utils — LSP layer (Phase 6.1 complete)
src/compiler/      compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                   parse_record, parse_cache — compiler front-end (Phase 4 complete)
src/db/            database, symbol_database, compilation_controller,
                   schema — SQLite persistence layer (Phase 5 complete, schema v3)
src/main.cpp       entry point
tests/unit/        Catch2 unit tests (268 cases, 672 assertions)
tests/integration/ Emacs functional test scripts (19 files, 84 test cases)
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
DISPLAY=:99 make test-integration   # runs all 17 test files
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

### Symbol utilities (`src/lsp/symbol_utils.h/.cpp`)

Shared helpers used by all LSP feature providers:

| Function | Description |
|---|---|
| `symbolKindFor(kind)` | Maps DB kind string → `lsp::SymbolKind` |
| `completionKindFor(kind)` | Maps DB kind string → `lsp::CompletionItemKind` |
| `wordAtPosition(text, line, char)` | Extracts identifier at 0-based cursor position; walks left even when cursor is on a non-id character (intentional — completion needs the prefix to the left of the insertion point) |
| `makeRange(line1, col0, nameLen)` | Converts 1-based line to 0-based `lsp::Range` |
| `pathToUri(path)` | Calls `lsp::FileUri::fromPath(path)` |

### LSP feature providers (Phase 6.1 — DB-backed)

Each lives in `src/lsp/<name>.h/.cpp`. All five providers now accept a
`SymbolDatabase&` and return real results from the SQLite database.

| File | Class | Signature | Behaviour |
|---|---|---|---|
| `hover.h/.cpp` | `HoverProvider` | `getHover(HoverParams, SymbolDatabase&, docText)` | `wordAtPosition` → `findSymbolsByName` → Markdown `**Kind** \`name\`` |
| `definition.h/.cpp` | `DefinitionProvider` | `getDefinition(DefinitionParams, SymbolDatabase&, docText)` | `wordAtPosition` → `findSymbolsByName` → `Location` |
| `completion.h/.cpp` | `CompletionProvider` | `getCompletion(CompletionParams, SymbolDatabase&, docText)` | `findSymbolsVisibleAt(path, line1)` → filter by prefix → `CompletionItem[]` |
| `document_symbols.h/.cpp` | `DocumentSymbolsProvider` | `getDocumentSymbols(DocumentSymbolParams, SymbolDatabase&)` | `symbolsForFile` → `DocumentSymbol[]` with scope ranges |
| `workspace_symbols.h/.cpp` | `WorkspaceSymbolsProvider` | `getWorkspaceSymbols(WorkspaceSymbolParams, SymbolDatabase&)` | `findSymbolsByNamePrefix(query)` → `WorkspaceSymbol[]` |
| `references.h/.cpp` | `ReferencesProvider` | `getReferences(ReferenceParams)` | Returns `nullptr` — Phase 6.2+ |
| `rename.h/.cpp` | `RenameProvider` | `getRename(RenameParams)` | Returns `nullptr` — Phase 6.2+ |
| `signature_help.h/.cpp` | `SignatureHelpProvider` | `getSignatureHelp(SignatureHelpParams)` | Returns `nullptr` — Phase 6.2+ |

Position-based providers (hover, definition, completion) check `m_store.contains(uri)`
before querying the DB and return `nullptr` if the document is not open.

### I/O wrapper (`src/lsp/server.h/.cpp`)

`LanguageServer` owns (in construction order):
`m_db`, `m_symbolDb`, `m_compiler`, `m_connection`, `m_messageHandler`, `m_store`, `m_diagnostics`.

`m_db` is opened as `":memory:"` — symbols persist across requests within one server session
but are lost on restart. The DB is re-populated on every `didOpen`/`didChange` via
`m_compiler.compile(path, text)`.

`registerHandlers()` wires all message types:

| Message | Kind | Handler |
|---|---|---|
| `initialize` | request | `handleInitialize` — stores processId, returns capabilities |
| `initialized` | notification | `handleInitialized` — no-op |
| `shutdown` | request | `handleShutdown` — transitions to Shutdown |
| `exit` | notification | `handleExit` — transitions to Inactive, breaks run() loop |
| `textDocument/didOpen` | notification | `m_store.open()` → `m_compiler.compile()` → `m_diagnostics.publish()` |
| `textDocument/didChange` | notification | `m_store.update()` → `m_compiler.compile()` → `m_diagnostics.publish()` |
| `textDocument/didClose` | notification | `m_store.close()` |
| `textDocument/hover` | request | `HoverProvider::getHover(params, m_symbolDb, docText)` |
| `textDocument/definition` | request | `DefinitionProvider::getDefinition(params, m_symbolDb, docText)` |
| `textDocument/references` | request | `ReferencesProvider::getReferences(params)` — null |
| `textDocument/completion` | request | `CompletionProvider::getCompletion(params, m_symbolDb, docText)` |
| `textDocument/documentSymbol` | request | `DocumentSymbolsProvider::getDocumentSymbols(params, m_symbolDb)` |
| `workspace/symbol` | request | `WorkspaceSymbolsProvider::getWorkspaceSymbols(params, m_symbolDb)` |
| `textDocument/rename` | request | `RenameProvider::getRename(params)` — null |
| `textDocument/signatureHelp` | request | `SignatureHelpProvider::getSignatureHelp(params)` — null |

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
| Database | SQLite3 (amalgamation, schema v3) | `src/db/schema.h` |
| SV directive taxonomy | Two-pass: strip compiler directives first, preprocess second | `docs/decisions/sv-preprocessor.md` (complete) |
| `__FILE__` / `__LINE__` | Resolved in pass 1 against original source, before include shifts line numbers | `plan.md §4.2b` |
| SV preprocessor tool | Minimal in-house C++ — slang upgrade path documented | `docs/decisions/sv-preprocessor.md` (complete) |
| SQLite ORDER BY after UNION ALL | Expressions like `length(scope)` are not allowed; sort in C++ | `src/db/symbol_database.cpp findSymbolsVisibleAt` |

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
| 3.2 | Diagnostics | `textDocument/publishDiagnostics` | Complete |
| 3.3 | Hover | `textDocument/hover` | Complete (real results — Phase 6.1) |
| 3.4 | Go-to-definition | `textDocument/definition` | Complete (real results — Phase 6.1) |
| 3.5 | Find references | `textDocument/references` | Wired (null — Phase 6.2+) |
| 3.6 | Completion | `textDocument/completion` | Complete (real results — Phase 6.1) |
| 3.7 | Document symbols | `textDocument/documentSymbol` | Complete (real results — Phase 6.1) |
| 3.8 | Workspace symbols | `workspace/symbol` | Complete (real results — Phase 6.1) |
| 3.9 | Rename | `textDocument/rename` | Wired (null — Phase 6.2+) |
| 3.10 | Signature help | `textDocument/signatureHelp` | Wired (null — Phase 6.2+) |

---

## Phase 4 status — Complete

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

### 4.2a Directive Taxonomy and Scope — Complete

Two-pass pipeline:
- **Pass 1 — compiler directive strip (§4.2b):** metadata directives, `__FILE__`, `__LINE__`
- **Pass 2 — preprocessor (§4.2c):** `` `define ``, `` `ifdef ``, `` `include ``, macro invocations

### 4.2b Compiler Directive Strip Pass — Complete

`src/compiler/compiler_directive_stripper.h/.cpp`, `tests/unit/compiler/test_compiler_directive_stripper.cpp`

### 4.2c Preprocessor Tool Selection and Integration — Complete

`src/compiler/sv_preprocessor.h/.cpp`, `tests/unit/compiler/test_sv_preprocessor.cpp`

### 4.3 AST Visitor / Listener — Complete

`SvTreeWalker` in `src/compiler/sv_tree_walker.h/.cpp`.

Scope stack tracks Module/Interface/Package/Class/Function/Task/**Program** — names are
pushed on enter hooks and popped on exit hooks. Each `ParseRecord` carries the `scope` chain
(e.g. `"MyModule::MyClass"`) and `endLine` (last line of scope body, for range building).
Exit hooks call `backpatchEndLine` to patch the record after the closing token is seen.
(`Program` support added in Phase 6.2 Stage 1 — previously programs weren't tracked at all.)

### 4.4 Symbol Extraction — Complete

`ParseRecord` fields: `kind, name, line, column, parent, detail, endLine, scope`.

- `endLine` — 1-based last line of scope body; 0 for leaf symbols (ports, signals, parameters)
- `scope` — full enclosing scope chain (e.g. `"MyModule::MyClass"`); `""` for top-level symbols

**`InstantiationRecord`** (Phase 6.2 Stage 1, `src/compiler/parse_record.h`): one per
module/interface/program instantiation (`Foo u0(...);`), fields `typeName, instName,
line, file`. Emitted by `enterModule_instantiation`/`enterInterface_instantiation`/
`enterProgram_instantiation` in `sv_tree_walker.cpp`; `WalkResult` carries it as
`instantiations`. This is a *reference*, not a declaration — it's how the Phase 6.2
library resolver (`-y`/`-v` filelist support, not yet implemented) will know which
instantiated type names aren't declared anywhere yet.

### 4.5 Error Recovery — Complete

`ParseError { line, column, message }`. `SvErrorListener` installed on lexer + parser.
`DiagnosticsPublisher::buildDiagnostic(ParseError)` converts to `lsp::Diagnostic`.

### 4.6 Incremental Parsing — Complete

`ParseCache` replaced by `CompilationController` (Phase 5.4) which uses SQLite hash storage.

---

## Phase 5 — SQLite Database Layer — Complete

### Schema v5

Defined in `src/db/schema.h`. `db::SCHEMA_VERSION = 5`.

Five tables:
- `files (id, path UNIQUE, content_hash, parsed_at)`
- `symbols (id, file_id→files, kind, name, line, col, parent, detail, end_line, scope)` —
  indexes on `name`, `file_id`, `scope`, `(scope,name)`, `(file_id,line,end_line)`
- `diagnostics (id, file_id→files, line, col, message)`
- `imports (id, file_id→files, pkg_name, item, is_export)` — `item = "*"` for wildcard
  imports; `is_export = 1` for `export pkg::item`/`export pkg::*` declarations;
  index on `file_id`
- `instantiations (id, file_id→files, type_name, inst_name, line)` — one row per
  module/interface/program instantiation (`Foo u0(...)`); indexes on `file_id`
  and `type_name`. Drives `unresolvedInstantiatedTypeNames()` (Phase 6.2's
  library resolver — see that section above).

Migrations run automatically in `database.cpp`:
- `MIGRATION_V1_TO_V2`: adds `end_line` and `scope` columns plus three new indexes
- `MIGRATION_V2_TO_V3`: adds the `imports` table and its index
- `MIGRATION_V3_TO_V4`: adds the `is_export` column to `imports` (default 0)
- `MIGRATION_V4_TO_V5`: adds the `instantiations` table and its two indexes

### 5.2 Database Abstraction Layer — Complete

`src/db/database.h/.cpp` — RAII `Database` wrapper around `sqlite3*`.

### 5.3 Query API — Complete

`src/db/symbol_database.h/.cpp`:

| Method | Description |
|---|---|
| `upsertFile(path, hash) → file_id` | INSERT or UPDATE; stable id for same path |
| `getFileHash(path) → string` | Returns `""` for unknown paths |
| `replaceSymbols(file_id, records)` | DELETE + INSERT in a transaction (9 columns incl. end_line, scope) |
| `replaceDiagnostics(file_id, errors)` | DELETE + INSERT in a transaction |
| `symbolsForFile(path) → vector<SymbolRow>` | Ordered by line |
| `findSymbolsByName(name) → vector<SymbolRow>` | Cross-file, with path |
| `diagnosticsForFile(path) → vector<DiagnosticRow>` | |
| `findSymbolsInScope(scope) → vector<SymbolRow>` | All symbols with exactly this scope value |
| `findSymbolsByNamePrefix(prefix) → vector<SymbolRow>` | LIKE `prefix%`, cross-file |
| `scopeAtPosition(path, line) → string` | Innermost scope-defining symbol containing `line` (1-based); returns `""` if top-level |
| `findSymbolsVisibleAt(path, line) → vector<SymbolRow>` | UNION ALL: (1) file-local scope chain, (2) cross-file top-level + wildcard-imported package scopes (plus their transitively re-exported packages), (3) one arm per specific import (plus re-exported specific items); C++ sorted by scope depth then name |
| `replaceImports(file_id, imports)` | DELETE + INSERT import records (incl. `is_export`) in a transaction |
| `importsForFileId(file_id) → vector<ImportRow>` | Returns `{ pkgName, item, isExport }` for the given file |
| `fileIdForPackage(pkgName) → int64_t` *(private)* | file_id of the file declaring top-level package `pkgName`, or -1 |
| `collectExportedImports(pkgName, ...)` *(private)* | Recursively follows `export pkg::*`/`export pkg::item` reachable from `pkgName`; cycle-safe via a `visited` list |
| `replaceInstantiations(file_id, insts)` | DELETE + INSERT instantiation records in a transaction |
| `unresolvedInstantiatedTypeNames() → vector<string>` | Distinct `type_name`s instantiated somewhere with no matching Module/Interface/Program declaration anywhere in the DB — drives Phase 6.2's library resolver |
| `appendDiagnostics(file_id, extra)` | INSERT-only (unlike `replaceDiagnostics`'s delete-then-insert) — lets the library resolver attach diagnostics without wiping a file's own ANTLR diagnostics |

`SymbolRow { id, kind, name, line, col, parent, detail, filePath, endLine, scope }`.

**SQLite UNION ALL ORDER BY limitation:** expressions like `length(scope)` are not
allowed in `ORDER BY` after a compound SELECT — only bare output column names are valid.
`findSymbolsVisibleAt` therefore omits the `ORDER BY` clause and sorts with `std::sort`
in C++ after fetching all rows.

### 5.4 Incremental Compilation Controller — Complete

`src/db/compilation_controller.h/.cpp` — hash check → skip or recompile → update DB.

### Library dependency graph

```
svlsp_compiler  (compiler_directive_stripper, sv_preprocessor, sv_tree_walker, parse_cache)
    → svlsp_antlr4

svlsp_db  (database, symbol_database, compilation_controller)
    → svlsp_sqlite3
    → svlsp_compiler

svlsp_lib  (lsp/*, no compiler sources)
    → lsp
    → svlsp_compiler
    → svlsp_db
```

---

## Preprocessor source map — Complete

`SvPreprocessor::process` returns a `sourceMap: vector<SourceLine>` alongside the
expanded text. Each entry maps one output line (index = line − 1) back to its
original `{file, line}`:

- `SourceLine.file == ""` → line belongs to the primary compiled file
- `SourceLine.file == "/path/to/inc.sv"` → line belongs to that included file

`SvTreeWalker::walk` consumes the map via `translateLine`, converting every
`ParseRecord` and `ParseError` to original-file coordinates before they leave the
compiler layer. `CompilationController::compile` groups by `file` and calls
`replaceSymbols`/`replaceDiagnostics` per distinct file, routing included-file
records to their own `file_id`.

Integration tests 15 (`test_15_preprocessor_lsp.sh`, 9 tests) verify that
hover, definition, completion, documentSymbol, and workspaceSymbol all report
correct paths and line numbers through the source map.

---

## Phase 6.3 — Package Import Resolution — Complete

`import pkg::*` (wildcard) and `import pkg::Foo` (specific) imports are now tracked
and used to extend `findSymbolsVisibleAt`.

### New components

**`ImportRecord`** (`src/compiler/parse_record.h`):
```cpp
struct ImportRecord {
    std::string pkgName;   // package being imported/exported
    std::string item;      // symbol name, or "*" for wildcard
    int         line{0};
    std::string file{};    // empty = primary compiled file
    bool        isExport{false}; // true for `export`, false for plain `import`
};
```

**ANTLR4 hook** (`SvRecordListener::enterPackage_import_item`):
Fires on every `import pkg::item` **and** `export pkg::item` statement (both
alternatives reuse the same `package_import_item` grammar production).
`enterPackage_export_declaration`/`exitPackage_export_declaration` toggle an
`m_inExport` flag around the export form so the emitted `ImportRecord` can be
stamped `isExport = true`. Translates the token line via the source map and
pushes onto `m_imports`. `WalkResult` gains a third field: `imports`.

**`imports` DB table** (schema v4):
`(id, file_id, pkg_name, item, is_export)` — `item = "*"` for wildcards,
`is_export = 1` for `export` declarations. `replaceImports` is called by
`CompilationController::compile` alongside `replaceSymbols`.
`MIGRATION_V3_TO_V4` adds the `is_export` column (default 0) to existing DBs.

**Extended `findSymbolsVisibleAt`** (`src/db/symbol_database.cpp`):
Loads the file's import records, then builds a three-part UNION ALL query:
1. File-local symbols in the scope chain (unchanged)
2. Cross-file symbols where `scope IN ('', ...wildcardPkgs)` — adds each
   wildcard-imported package scope to the permitted set
3. One additional UNION ALL arm per specific import: `scope = pkg AND name = item`

**Export re-exports** (`SymbolDatabase::collectExportedImports`, cycle-safe via
a `visited` list): for each directly wildcard-imported package, looks up that
package's own declaring file (`fileIdForPackage`) and follows its `export
pkg::*` / `export pkg::item` declarations, merging re-exported wildcard
packages and specific items into the same `wildcardPkgs`/`specificImports`
sets used above. **Plain (non-exported) imports are never followed** — only
the immediately imported scope is visible unless that scope explicitly
re-exports it. Only the `export pkg::item` / `export pkg::*` grammar
alternative is handled; the LRM's `export *::*;` shorthand (re-export
everything imported into the current scope, regardless of package) is not
wired up — that literal doesn't route through `package_import_item` at all.

### Tests

Unit: `[import]`/`[export]` test cases across `test_sv_listener.cpp` (export
vs. plain-import tagging) and `test_symbol_database.cpp` (transitive-export
resolution, plus a regression test proving a plain import does *not* leak a
second-level import).

Integration (`test_17_import_resolution.sh`, 10 tests):
- Prerequisite: open `util_pkg.sv` to seed DB
- Wildcard: completion includes all three util_pkg symbols (DataItem, Logger, compute)
- Wildcard: hover on DataItem → non-null; definition → util_pkg.sv at LSP line 2
- Specific: completion includes DataItem; excludes Logger and compute

Fixtures: `tests/integration/fixtures/{util_pkg,import_wildcard,import_specific}.sv`

Integration (`test_18_export_resolution.sh`, 10 tests):
- Prerequisites: seed `base_pkg.sv`, `middle_pkg.sv` (imports + exports
  `base_pkg::*`), and `plain_middle_pkg.sv` (imports `base_pkg::*`, no export)
- Export: completion in `export_user.sv` (imports only `middle_pkg::*`)
  includes both `Beta` (middle_pkg's own) and `Alpha` (re-exported from
  `base_pkg`); hover/definition on `Alpha` confirm it resolves to
  `base_pkg.sv` at its true declaration line — not merely that a same-named
  symbol is visible
- Regression: completion in `plain_import_user.sv` (imports only
  `plain_middle_pkg::*`, which does *not* export) includes `Gamma` but
  excludes `Alpha` — proving plain imports still don't leak transitively

Fixtures: `tests/integration/fixtures/{base_pkg,middle_pkg,export_user,plain_middle_pkg,plain_import_user}.sv`

---

## Phase 6.2 — Multi-File Project Support — Complete (6/6 stages)

**Full plan file (read this first to resume):**
`/home/martin/.claude/plans/fluffy-hatching-popcorn.md` — contains the complete
approved design: exact struct/method signatures, schema SQL, the library-resolution
fixpoint algorithm spelled out step-by-step, file/test naming, and PR sequencing.
This section is a status summary only; the plan file is the source of truth.

### Scope (confirmed with the user)

- Support **two** independent project-config formats, both producing one shared
  `ProjectConfig` struct: a custom, extensible `.svlsp.json` manifest, **and** a
  VCS/Questa/Xcelium-style `.f` filelist (for interop with existing EDA build flows).
- The filelist parser implements **full `-y`/`-v`/`+libext+` library resolution**
  (auto-discover a module's defining file by name when referenced/instantiated
  but not explicitly listed) — not a stub.
- Any `.f` switch not explicitly supported is a **hard error**, not silently ignored.
- Unresolved instantiations (not found in project files, `-v` files, or `-y` dirs)
  **emit a diagnostic** on the referencing file, reusing the existing `ParseError`
  pipeline (confirmed with the user — see plan file §Stage 4).

### Stage status

| Stage | What | Status |
|---|---|---|
| 1 | Program tracking (`ParseRecordKind::Program`) + `InstantiationRecord` + schema v5 (`instantiations` table) + `unresolvedInstantiatedTypeNames`/`appendDiagnostics` | **Complete** — commit `fe0f817`, 15 new unit tests, full suite 268 cases/672 assertions passing |
| 2 | `.f` filelist parser (`src/compiler/filelist_parser.h/.cpp`, `ProjectConfig` in `src/compiler/project_config.h`) | **Complete** — 13 new unit tests, full suite 281 cases/702 assertions passing |
| 3 | `.svlsp.json` manifest parser (`src/lsp/project_manifest_parser.h/.cpp`, via `lsp::json`) | **Complete** — 10 new unit tests, full suite 291 cases/738 assertions passing |
| 4 | Thread `ProjectConfig` into `CompilationController::compile`; `LibraryResolver` (-v/-y fixpoint); `ProjectCompiler` batch loader | **Complete** — 14 new unit tests, full suite 305 cases/769 assertions passing |
| 5 | Server wiring: capture `rootUri`/`initializationOptions` in `ServerState`; new `ProjectRegistry` (upward-search discovery, caching, lazy load) | **Complete** — 12 new unit tests, full suite 317 cases/790 assertions passing; full Emacs integration suite rerun (89 passed, 6 failed — all 6 pre-existing/documented, zero new regressions) |
| 6 | End-to-end Emacs test `test_21_multifile_project.sh` + `multifile_project/` fixtures (renumbered from `test_19` after two unrelated macro-expansion regression tests were inserted — see "Known gaps") | **Complete** — 4 new integration cases; also found and fixed a real bug (see below) |

### Key facts discovered during planning (still true, don't re-derive)

- `program` declarations were **not tracked at all** before Stage 1 — now fixed
  (mirrors Module/Interface hooks exactly; grammar rules confirmed at
  `grammar/Sv.g4:89-101,3673`).
- **Hover/Definition already do global cross-file lookup** via `findSymbolsByName`
  (no scope/file filtering) — so once a library-resolved file's symbols land in
  the DB, hover/definition on an instantiation site work with **zero changes**
  to `hover.cpp`/`definition.cpp`. The only new capability needed is the
  *resolver* knowing which names to search for.
- No JSON library is linked except lsp-framework's own `lsp::json` (confirmed
  API: `isObject()`/`object()`/`find()`/`isString()`/`string()`) — already
  transitively available via `svlsp_lib`, but **not** via `svlsp_compiler`
  (confirmed: `svlsp_compiler` links only `svlsp_antlr4`/`svlsp_options`). This
  is why the JSON manifest parser must live under `src/lsp/`, while the filelist
  parser belongs in `src/compiler/`.
- `InitializeParams` (generated `types.h:6080-6159`) has `rootUri`
  (`NullOr<DocumentUri>`), `rootPath` (`Opt<NullOr<String>>`),
  `initializationOptions` (`Opt<LSPAny>`, `LSPAny = json::Value`), and
  `workspaceFolders` — all currently unread anywhere in the codebase.
- Filelist format confirmed via web research (VCS/Questa/Xcelium): bare
  filenames; `+define+NAME[=VALUE]` and `+incdir+DIR` chainable on `+`;
  `-f FILE` (nested, CWD-relative) vs `-F FILE` (nested, relative to the
  filelist's own dir); `-sv`/`-sverilog`; `-y DIR`; `-v FILE`; `+libext+.ext`
  chainable; `-top MODULE`; `//` comments; double-quoted filenames.

### Stage 2 — `.f` filelist parser — Complete

`src/compiler/project_config.h` (new, header-only `ProjectConfig`/`SvLanguageMode`)
and `src/compiler/filelist_parser.h/.cpp` (new, `FilelistParser::parse(path)`),
both registered in `CMakeLists.txt` under `svlsp_compiler`.

- **CWD-relative vs. file-relative resolution is implemented via a per-recursion-frame
  `baseDir` string**, not a global. `-f FILE`: recurses with the *same* `baseDir` as the
  current frame (paths inside the nested file stay CWD-relative, matching vendor tool
  behavior). `-F FILE`: recurses with `baseDir` = the nested file's own parent directory.
  The top-level `parse(path)` call seeds `baseDir = fs::current_path()` — i.e. the entry
  point behaves as if it were itself `-f`'d in from the CWD.
- **Cycle detection uses an "active recursion stack" set** (`insert` on entry,
  `erase` on return), not a permanent "ever visited" set — so a diamond include
  (A includes B and C; both B and C include D) is legal and D is parsed twice
  (harmless: `compile()` is content-hash-cached downstream), while true cycles
  (A → B → A) throw. Don't switch this to a permanent-visited set without checking
  this distinction is still wanted.
- Any `-x`/`+x` token not in the explicitly supported list throws
  `std::runtime_error` naming the offending token and `path:line` — no silent
  ignoring, per the confirmed scope above.
- Unit tests: `tests/unit/compiler/test_filelist_parser.cpp`, tag `[compiler][filelist]`
  — 13 cases covering every bullet in the Context section's format list, using real
  temp files under `/tmp/svlsp_test_filelist/` (nested `-f`/`-F` targets must exist on
  disk since the parser opens them to canonicalize for cycle detection).

### Stage 3 — `.svlsp.json` manifest parser — Complete

`src/lsp/project_manifest_parser.h/.cpp` (new, `ProjectManifestParser::parse(path)`),
registered in `CMakeLists.txt` under `svlsp_lib` (not `svlsp_compiler` — needs `lsp::json`,
confirmed unavailable there; see "Key facts" above).

- `lsp::json::parse`'s `ParseError` and `Value::string()`/`object()`'s `TypeError`
  both derive from `lsp::Exception → std::runtime_error`, so they already satisfy
  "throws `std::runtime_error`" — caught once at the top of `parse()` and rewrapped
  with the manifest path prepended for a clearer message; every other validation
  (wrong-typed field, non-object root, invalid `"mode"` value) throws its own
  `std::runtime_error` directly, naming the offending field.
- **Unknown top-level keys are silently ignored** (JSON is self-describing — a
  typo'd key can't corrupt parsing of an unrelated field), the deliberate opposite
  of the filelist parser's hard-error policy — see the plan file's design note if
  you want to revisit that asymmetry.
- `"mode"` only accepts the literal strings `"sv"` / `"v95"`; anything else throws
  (not in the original plan spec, added defensively since an unrecognized mode
  string silently keeping the default would be a worse failure mode than an error).
- Relative paths in `"files"`/`"includeDirs"`/`"libraryDirs"`/`"libraryFiles"`
  resolve against the manifest's own parent directory (`libExtensions` values are
  bare extension strings, not paths — no resolution).
- Unit tests: `tests/unit/lsp/test_project_manifest_parser.cpp`, tag
  `[lsp][project-manifest]` — 10 cases: full/partial fields, `"mode":"v95"`,
  unknown key ignored, malformed JSON, non-object root, wrong-typed `"files"` and
  `"defines"` values, invalid `"mode"` value, missing file on disk.

### Stage 4 — Config threading + library resolution — Complete

- **`CompilationController::compile`** gained a 3rd parameter
  `const ProjectConfig* config = nullptr`. Every existing call site (`server.cpp`,
  all pre-Stage-4 unit tests) is untouched — the default preserves exactly the old
  behavior (bare `SvPreprocessor`, no defines/include dirs). When non-null,
  `config->includeDirs` seeds the `SvPreprocessor` constructor and each
  `config->defines` entry is applied via `.define(name, value)` before `.process()`.
- **`src/compiler/file_utils.h/.cpp`** (new, under `svlsp_compiler`): one function,
  `readFile(path) -> std::optional<std::string>`, returning `nullopt` (not throwing)
  on a missing file — both `LibraryResolver` and `ProjectCompiler` treat a missing
  file as "skip", never a hard error, unlike the filelist/manifest parsers.
- **`SymbolDatabase::instantiationsOfType(typeName) -> vector<InstantiationRow>`**
  (new; `InstantiationRow{fileId, filePath, line}`) — added because Stage 4 needed
  "every file referencing an unresolved name" and no existing query provided it;
  not spelled out with an exact signature in the plan file, so this is a Stage-4
  design decision, not something to hunt for in the plan doc.
- **`src/db/library_resolver.h/.cpp`** (new): `LibraryResolver::resolve(config,
  controller, sdb)`. Implementation detail worth knowing — it resolves **one name
  at a time**, re-querying `unresolvedInstantiatedTypeNames()` from scratch after
  every single compile, rather than resolving a whole batch before re-querying (the
  plan sketches a batch-per-round shape). Chosen because re-checking after each
  compile is trivially correct (a name that a same-round compile happens to resolve
  is never redundantly re-attempted) at a cost of a few extra cheap `SELECT`s — not
  a deviation in observable behavior, just a simpler loop. Termination still rests
  on the same two facts the plan calls out: a name in `failedNames` is never
  retried, and `compiledPaths` prevents recompiling the same resolved file twice.
  `-v` files are pre-indexed by a raw parse (`CompilerDirectiveStripper` →
  `SvPreprocessor` → `SvTreeWalker`, using `config.includeDirs`/`config.defines` so
  they preprocess consistently with the rest of the project) that is **not**
  persisted to the DB — only a file that's actually resolved against gets
  `controller.compile()`'d. `-y` search order is strictly `libraryDirs` outer loop
  × `libExtensions` inner loop, first `readFile()` hit wins (verified by a test
  fixture where two candidates declare the same module name but differ in a
  secondary nested instantiation, so whichever version "won" is observable).
- **`src/db/project_compiler.h/.cpp`** (new): `ProjectCompiler::loadProject(config,
  controller, sdb)` reads and compiles every `config.files` entry (missing ones
  silently skipped, mirroring `readFile`'s no-throw contract), then calls
  `LibraryResolver::resolve`. Returns total files compiled (explicit + library).
- Unit tests: 3 new cases appended to `tests/unit/db/test_compilation_controller.cpp`
  (tag `[db][ctrl][project-config]`, directly exercising the new 3-arg `compile()`
  overload — not explicitly named in the plan's Stage 4 test list but added for
  direct unit-level coverage of the config-threading change itself, separate from
  the higher-level `ProjectCompiler`/`LibraryResolver` coverage the plan does call
  for). `tests/unit/db/test_library_resolver.cpp` (new, tag `[db][library-resolver]`,
  6 cases: `-v` resolution, `-y` dir-order and extension-order preference, A→B→C
  fixpoint chain, dead-end name stays unresolved with zero files compiled, diagnostic
  attached to the referencing file). `tests/unit/db/test_project_compiler.cpp` (new,
  tag `[db][project-compiler]`, 5 cases: explicit files loaded, missing file skipped,
  config defines/includeDirs affect preprocessing, `LibraryResolver` invoked
  end-to-end through `loadProject`).

### Stage 5 — Server wiring — Complete

- **`ServerState`** (`src/lsp/server_state.h/.cpp`) captures two things during
  `handleInitialize`: `m_rootUri` (raw `lsp::NullOr<lsp::DocumentUri>`, exposed via
  `rootUri()` — captured for completeness only, **not** consumed anywhere; see
  `ProjectRegistry`'s header comment for why upward search was chosen over it) and
  `m_explicitProjectConfigPath` (a `std::string`, "" if absent), extracted from
  `initializationOptions.svlsp.projectConfig` by a private static
  `extractProjectConfigPath` helper that returns "" (never throws) at every step
  where the shape doesn't match: options absent, options not an object, no
  `"svlsp"` key, `"svlsp"` not an object, no `"projectConfig"` key, or
  `"projectConfig"` not a string.
- **`src/lsp/project_registry.h/.cpp`** (new): `ProjectRegistry::configFor(filePath)`
  is the single entry point. Discovery order: (1) `m_explicitConfigPath` if set via
  `setExplicitConfigPath` — wins unconditionally, no filesystem walk at all; (2)
  upward search from `filePath`'s own directory, checking `.svlsp.json`,
  `svlsp.json`, `.svlsp.f`, `svlsp.f`, `files.f` in that precedence at each
  directory level before moving to the parent, stopping at the filesystem root;
  (3) `nullptr` if nothing found (today's single-file behavior, byte-for-byte
  unchanged — this is why every pre-Stage-5 integration test still passes
  unmodified). Parser dispatch is by suffix: paths ending in `.json` go through
  `ProjectManifestParser`, everything else through `FilelistParser`.
- **Caching is keyed by the discovered config file's own path**, not a separately
  computed "root directory" — the config file's parent directory *is* the root
  for every practical purpose here, so this sidesteps computing/normalizing a
  second identity for the same thing. First `configFor` call for a given config
  path parses it and runs `ProjectCompiler::loadProject` (which itself internally
  invokes `LibraryResolver`); every subsequent call for any file under that root
  returns the same cached `ProjectConfig*` with no re-parse and no re-`loadProject`
  — verified in the unit tests by pointer-identity equality across two different
  files under one discovered root. **A directory with no manifest anywhere in its
  ancestry is not cached as a negative result** — each such `configFor` call redoes
  the (cheap) upward filesystem walk; deliberately not optimized further since
  redoing a `fs::exists` walk per edit is not the bottleneck anywhere in this
  codebase yet.
- **`server.h/.cpp`**: `LanguageServer` gained `m_projects` (constructed right
  after `m_compiler`/`m_symbolDb`, matching the plan's ordering requirement since
  `ProjectRegistry`'s constructor takes references to both). `parseDiagnostics`
  now calls `m_compiler.compile(path, text, m_projects.configFor(path))` — for
  any file with no discoverable project, `configFor` returns `nullptr`, which
  `compile`'s defaulted 3rd parameter already treats as "no project" (Stage 4),
  so this is a no-op change for every currently-passing integration test. The
  `Initialize` handler calls `m_projects.setExplicitConfigPath(m_state.
  explicitProjectConfigPath())` immediately after `m_state.handleInitialize` —
  unconditionally (an empty string is a harmless no-op, since that's already
  `ProjectRegistry`'s default).
- Unit tests: 6 cases appended to `tests/unit/lsp/test_server_state.cpp` (tag
  `[lsp][server-state][project]` — rootUri capture, and every
  present/absent/malformed shape of `explicitProjectConfigPath` extraction).
  `tests/unit/lsp/test_project_registry.cpp` (new, tag `[lsp][project-registry]`,
  6 cases: no-manifest-found, upward search into a parent directory,
  closest-directory-wins over an outer manifest, `.svlsp.json`-over-`svlsp.f`
  precedence in the same directory, load-once caching via pointer identity,
  explicit-path override regardless of the file's own directory tree).
- **Verification beyond unit tests**: the full Emacs integration suite (all
  `test_*.sh` files) was rerun after this stage — 89 passed, 6 failed, and all 6
  failures are the pre-existing ones already documented in "Known gaps" below
  (5 from the macro mid-line/multi-line column-drift gaps, 1 unrelated
  `test_08_completion.sh` flake) — zero new regressions from the server wiring.

### Stage 6 — End-to-end Emacs integration test — Complete

New fixture `tests/integration/fixtures/multifile_project/` (`.svlsp.f` = `top.sv`
+ `-y libs` + `+libext+.sv`; `top.sv` instantiates `leaf_mod` without declaring or
listing it anywhere; `libs/leaf_mod.sv` declares it, reachable only via `-y`
search). New `tests/integration/test_21_multifile_project.sh`, 4 cases: hover and
definition on the `leaf_mod` instantiation site in `top.sv`, proving the
library-resolved file's symbols are reachable with **zero changes** to
`HoverProvider`/`DefinitionProvider`.

**Real bug found and fixed while writing this test** (not a pre-existing/documented
gap like the macro ones — this one is fixed, not deferred): the first attempt at
this test failed all 4 cases. Manual JSON-RPC probing against the `svlsp` binary
directly (bypassing Emacs to narrow the search space) showed zero diagnostics but
a null hover — i.e. `leaf_mod` was never actually getting compiled at all.
Root cause: `FilelistParser::parse(path)` resolves every bare relative path (`-y
libs`, the bare `top.sv` entry) against `fs::current_path()` — correct for the
plan's original CLI-tool framing ("the top-level call behaves as if it were
itself `-f`'d in from the CWD"), but meaningless for `ProjectRegistry`'s
auto-discovery use case, where the server process's CWD has no relation to
wherever a discovered `.svlsp.f` happens to live. `ProjectRegistry::loadAndCache`
was calling `FilelistParser::parse(configPath)` with no way to override that.
**Fix**: `FilelistParser::parse` gained a second parameter, `baseDir = ""` (empty
means "behave exactly as before, i.e. CWD" — every pre-existing call site and unit
test is unaffected); `ProjectRegistry::loadAndCache` now passes
`fs::path(configPath).parent_path().string()` explicitly. `ProjectManifestParser`
needed no equivalent change — Stage 3 already resolves its own paths against the
manifest's own directory internally, which is why this asymmetry between the two
parsers wasn't visible until a filelist with actual relative paths was exercised
through discovery.
This also means the Stage 5 unit tests had a real coverage gap: every
`test_project_registry.cpp` case up to this point used `.svlsp.json` fixtures
with only a `"top"` string field — never a path-bearing field through
`FilelistParser`, so the bug went undetected until this end-to-end test forced a
real `-y`/bare-filename resolution through the discovery path. Two regression
tests were added to close this gap: `test_filelist_parser.cpp` ("relative bare
filenames resolve against an explicit baseDir, not CWD" / "... when baseDir is
omitted") and `test_project_registry.cpp` ("a discovered .svlsp.f's relative
paths resolve against its own directory, not the server's CWD").

**Deviation from the plan, deliberate**: the plan additionally called for
exercising `ProjectRegistry`'s explicit-path override end-to-end via a dynamic
`svlsp-test/initialization-options` Elisp variable (wired into
`tools/emacs-test-init.el`'s `:initialization-options`, reset to nil afterward).
That variable **is** wired up as specified, but `test_21` does not use it:
lsp-mode reuses one workspace/server process per detected project root, every
fixture in this repo resolves to the same git-root workspace, and that
workspace's one-time `initialize` handshake (and thus `ServerState::
explicitProjectConfigPath`) already happened earlier in the suite (as early as
`test_02`) — mutating the variable at `test_21` time has no effect without a
`lsp-workspace-restart`, which risks destabilizing every test after it. The
explicit-path-override behavior itself is already fully covered at the unit
level (`test_project_registry.cpp`, "an explicit config path overrides upward
search for every file"). `test_21` instead proves the upward-search discovery
path — Stage 5's actual behavior for every real editor session, since nobody
hand-configures `initializationOptions` in practice.

Full verification after this stage: unit suite 320 cases/800 assertions (all
green); full Emacs integration suite 93 passed / 6 failed — the same 6
pre-existing/documented failures as before this stage, plus all 4 new `test_21`
cases passing.

### Post-Stage-6 — Extensive combined-features integration test — Complete

`tests/integration/test_22_full_project.sh` + `fixtures/full_project/` — a single
project fixture deliberately combining every Phase 6.2/6.3 mechanism at once
(`` `include ``, an explicit project file, `-y` library resolution, a wildcard
import, a specific import, and transitive export), checked against every LSP
feature: diagnostics, hover, definition, completion, documentSymbol, and
workspace/symbol. 26 test cases, all passing.

- **`.svlsp.f` must list the package files explicitly** (`fp_util_pkg.sv`,
  `fp_reexport_pkg.sv`), alongside `fp_top.sv`/`fp_extra_mod.sv` and `-y libs`.
  First attempt omitted them (reasoning "packages aren't instantiated like
  modules, so they don't need `-y`/`-v` resolution") — wrong: import/export
  resolution is a **DB lookup by package name** (`fileIdForPackage`), with *no*
  library-resolution-style mechanism to go find a package's declaring file on
  demand the way `LibraryResolver` does for module instantiations. All 10
  import/export/completion/workspaceSymbol cases failed until the two package
  files were added to the explicit file list — a real project's filelist/manifest
  must list every source file, packages included, not just modules that get
  instantiated.
- **All symbol names are `fp_`-prefixed** (`fp_top`, `fp_sub_block`, `fp_extra_mod`,
  `fp_leaf_mod`, `FpWidget`, `FpExtra`, `fp_compute`) and were checked against
  every existing name across `tests/integration/fixtures/**` and `examples/**`
  before writing the fixture. This matters because **the entire Emacs daemon
  session shares one svlsp server process and one growing in-memory DB across
  every `test_*.sh` file** — e.g. this fixture's library-resolved module could
  not be named `leaf_mod` (test_21's `multifile_project` fixture already uses
  that exact name), since `findSymbolsByName`/hover/definition would then have
  to arbitrarily pick between two same-named symbols in two unrelated files.
  Any future fixture must do the same name-collision check against the whole
  tree, not just its own subdirectory.
- Deliberately does **not** assert on the macro-affected wire (`fp_top_bus`,
  driven by `` `FP_BUS_WIDTH `` mid-declaration): its column would hit the
  known, documented mid-line-macro column-drift gap (test_19/test_20). Zero
  diagnostics is used as the (sufficient) proof that the macro expanded
  correctly — a broken expansion produces a parse error, per that same gap's
  root cause.
- Full verification: unit suite unchanged at 320 cases/800 assertions
  (integration-only change); full Emacs integration suite 119 passed / 6
  failed — the same 6 pre-existing/documented failures, plus all 26 new cases
  passing, confirmed via a second full run to rule out flakiness.

---

## Phase 6.1 — DB-Backed LSP Providers — Complete

All five active providers rewritten to query `SymbolDatabase`:

- **DocumentSymbols** (`symbolsForFile`) — builds `DocumentSymbol[]`; scope symbols get a
  multi-line `range` (`endLine`-based) and a point `selectionRange` at the identifier.
  Leaf symbols (endLine = 0) get `range == selectionRange`.
- **WorkspaceSymbols** (`findSymbolsByNamePrefix`) — builds `WorkspaceSymbol[]` with `Location`.
- **Hover** (`wordAtPosition` + `findSymbolsByName`) — prefers same-file match; returns
  Markdown `**Kind** \`name\`` with optional detail and scope.
- **Definition** (`wordAtPosition` + `findSymbolsByName`) — returns first matching `Location`.
- **Completion** (`findSymbolsVisibleAt`) — scope-aware; filters by any already-typed prefix;
  returns `CompletionItem[]` with `completionKindFor` and optional `detail`.

Integration tests 05/06/08/09/10 updated from "expect null" to verify real results.

### Phase 6 sub-phase status

| Sub-phase | Feature | Status |
|---|---|---|
| 6.1 | DB-backed LSP providers | **Complete** |
| 6.2 | Multi-file project support (`.svlsp.json` + `.f` filelist, incl. `-y`/`-v` library resolution) | **Complete** (6/6 stages), see Phase 6.2 section above |
| 6.3 | Package import/export resolution (`import pkg::*`, `export pkg::*`) | **Complete** |
| 6.4 | Cross-file invalidation (dependency graph) | Not started — see `plan.md §6.4` |
| 6.5 | Performance baseline | Not started |
| 6.6 | Packaging / `make install` | Not started |

---

## Sv.g4 grammar quirks (discovered in Phase 4.2)

| Construct | Expected SV | Grammar behaviour | Workaround |
|---|---|---|---|
| Backtick directives | `` `define ``, `` `ifdef ``, `` `timescale `` | Not in grammar at all — no lexer rules | Must be preprocessed before parsing (Phase 4.2a) |
| `bind` double semicolon | `bind M C u (.p(p));` | `bind_directive` adds `';'` on top of `module_instantiation`'s own `';'` | Write `bind M C u (.p(p));;` |
| `bind` parameter override | `bind M C #(.W(W)) u (.p(p));;` | LL(\*) prediction fails after `#(...)` | Omit parameter override; use default params |
| Cross body `ignore_bins` | `cross A, B { ignore_bins x = ...; }` | `cross_body_item` already consumes `';'`, then `cross_body` adds another — double semicolon | Use `cross A, B;` (empty cross body) |
| `timeunit`/`timeprecision` vs `` `timescale `` | `` `timescale 1ns/1ps `` | No backtick directive support | Use `timeunit 1ns; timeprecision 1ps;` inside module |

**Fixed, no longer a quirk:** string literal escapes (`"a \"quoted\" word"`) —
`STRING_LITERAL` previously had no escape-sequence awareness (`'"' .*? '"' ;`,
terminated at the first embedded `"` regardless of a preceding `\`). Found and fixed
2026-08-21 (committed `b5cda7c`; see "UVM real-world smoke test — session 3" §
"`STRING_LITERAL` escape-sequence gap — FIXED" above) — now
`` '"' ( '\\' . | ~["\\] )* '"' ``.

**Fixed, no longer a quirk:** void cast (`void'(f())`) — `subroutine_call_statement`
previously required `'void' '(' subroutine_call ')' ';'` with no `SINGLE_QUOTE`, so
the LRM-correct `void'(...)` form (used pervasively in real UVM) failed to parse;
only the workaround `void(f())` form (no longer needed, but still accepted) worked.
Found and fixed 2026-08-21 (uncommitted; see "UVM real-world smoke test — session 3"
§"`void'(...)` cast gap — FIXED" below) — now
`` 'void' SINGLE_QUOTE? '(' subroutine_call ')' ';' ``.

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
- `CompilationController` uses `":memory:"` SQLite — symbols are lost on server restart.
  Each file must be re-opened for its symbols to reappear. Cross-session persistence
  requires a file-backed DB path (straightforward swap, just change the path in `server.cpp`).
- The `export *::*;` LRM shorthand (re-export everything imported into the current scope,
  regardless of package) is not implemented. Only `export pkg::*` / `export pkg::item` are
  handled — see Phase 6.3 section above. That literal alternative doesn't route through the
  `package_import_item` grammar rule at all, so `SvRecordListener` silently ignores it.
- References, rename, and signature help providers still return `nullptr`. These are next
  after Phase 6.3/6.4.
- **Mid-line macro expansion column drift — Fixed (2026-07-19).** `SvPreprocessor`'s
  source map (`sourceMap`) previously only translated line numbers across macro
  expansion; it never adjusted columns for the text-length delta a macro invocation
  introduces mid-line. Fix: `SourceLine` (`src/compiler/parse_record.h`) gained a
  `colShifts: vector<ColShift>` field — `ColShift{outputCol, delta}` breakpoints,
  one per macro invocation on that output line, where `delta = invocationLen -
  replacementLen`. `expandStr` (`sv_preprocessor.cpp`) now accepts an optional
  `vector<ColShift>*` (only passed at the top-level per-line call, `depth == 0` —
  recursive calls into a macro's own body pass `nullptr`, since a nested invocation's
  own span already collapses into the outer invocation's net `replacementLen`) and
  appends a breakpoint after each expansion; multiple macros on one line accumulate
  a running delta. `sv_tree_walker.cpp` gained `translateColumn(compiledLine,
  compiledCol, map)` (linear scan over that line's `colShifts`, applied alongside
  the existing `translateLine`) and both `pushId()` (all `ParseRecord`s) and
  `SvErrorListener::syntaxError` (all `ParseError`s) now translate columns, not
  just lines. `tests/integration/test_19_macro_midline_expansion.sh` — all 5 cases
  now pass (previously 2 failing). As a side effect this also fixed 2 of the 3
  previously-failing column cases in `test_20_macro_multiline_midline.sh`
  (see below, also since fixed) — only that test's diagnostics case needed the
  separate multi-line-`` `define ``-body fix below.
  Unit coverage: `tests/unit/compiler/test_sv_preprocessor.cpp` (4 new `[sourcemap]`
  cases — shrinking macro, growing macro, no-macro line, two-macros-on-one-line
  delta accumulation) and `tests/unit/compiler/test_sv_listener.cpp` (2 new
  `[sourcemap]` cases exercising `translateColumn` through a full `walk()`).
- **Multi-line (backslash-continuation) `` `define `` bodies — Fixed (2026-07-19).**
  `SvPreprocessor::processSource` used to read and expand strictly one physical line at
  a time (`std::getline` loop) with no check for a trailing `\` on a `` `define `` line
  — the continuation line was emitted as ordinary source code instead of being merged
  into the macro body, and the literal trailing backslash was left in the body text,
  producing a stray `\` in the expanded output and a spurious ANTLR parse error.
  Fix: inside the `dir == "define"` branch of `processSource`, after copying `rest`
  into a local `std::string mergedRest`, a loop strips a trailing `\` and appends
  (via nested `std::getline` calls, bypassing the outer per-line loop) each further
  physical line directly — no separator inserted, matching the C-preprocessor
  splicing rule — until a line without a trailing `\` is read; `parseMacroDefinition`
  then runs once on the fully merged body. Each consumed physical line (the `` `define ``
  line itself, plus every continuation line) still gets its own `emitBlank()` call so
  the source map stays 1:1 with input line count; the macro's recorded `line` is the
  *starting* `` `define `` line, captured before the merge loop mutates `lineNo`.
  Scoped to `` `define `` only (per the LRM, other directives could theoretically use
  continuation too, but no other directive here reads a multi-token body, so this
  wasn't extended speculatively). `tests/integration/test_20_macro_multiline_midline.sh`
  — all 6 cases now pass (previously 3 failing: diagnostics, declaration column,
  go-to-definition column — the latter two already fixed by the mid-line column-drift
  fix above, this change closes the remaining diagnostics case).
  Unit coverage: `tests/unit/compiler/test_sv_preprocessor.cpp` (4 new `[multiline]`
  cases — two-line merge, three-line chained merge, output-line-count preservation,
  and column-shift correctness when a multi-line-defined macro is invoked mid-line).
  Full verification (both fixes together): unit suite 330 cases/848 assertions (all
  green); full Emacs integration suite 124 passed / 1 failed — the sole remaining
  failure is the pre-existing, unrelated `test_08_completion.sh` flake (confirmed by
  running it in isolation), zero new regressions from either fix.
