# svlsp — Handoff Document

**Last updated:** 2026-09-13 (compressed from full session history — see git log for
narrative detail if ever needed; this file now documents current-state-and-next-steps
only).

**Implemented 2026-09-13** — real `references`/`rename`/`signatureHelp` (plan.md
§6.22, closing "Not yet done" #3 below), replacing the three long-standing
unconditional-`null` stubs. None of the three had any data model to build on
directly (no reference-tracking table, only declarations; no per-parameter data
for function/task calls), so each was scoped to a genuinely useful but honestly
limited v1 rather than building the full "real" semantic version:

- **References/rename**: a lexical, cross-file text search, not scope-aware.
  New `SymbolDatabase::allFilePaths()` plus `symbol_utils.h`'s
  `findIdentifierOccurrences(text, name)` (skips comments/string literals, same
  in-string handling the preprocessor's own macro-argument scanner uses).
  `ReferencesProvider`/`RenameProvider` take an injected `textForPath` callback
  (open-buffer-or-disk) rather than touching `DocumentStore` directly, reusing
  `LanguageServer::currentTextFor` — the same helper §6.4's
  `forceRecompileAndPublish` already needed, extracted for this second use.
  `RenameProvider` validates `newName` against IEEE 1800's identifier grammar,
  throwing `lsp::RequestError` otherwise. Disclosed limitation: two unrelated
  same-named declarations in different files are indistinguishable from here
  and both get included/renamed — a real fix needs the same semantic
  reference-resolution machinery plan.md §6.21 already scopes out separately.
- **Signature help**: scoped to module/interface/program instantiation port
  lists only (reuses existing `Port` symbols — name + direction). A local
  backward-paren-depth scan finds the enclosing argument list and the
  `<TypeName> <InstanceName> (` header; active-parameter tracking counts
  top-level commas but also recognizes a named port connection (`.portName(`)
  and resolves it by name instead when present — a real subtlety found
  implementing this: a named connection's own parens must be distinguished
  from a genuine nested call (`obj.method(`) or the scan stops at the wrong
  paren; `isNamedConnectionParen` checks what precedes the leading `.`
  (`(`/`,`/start-of-text` vs. an identifier) to tell them apart. Function/task
  calls are explicitly NOT supported — no per-parameter data exists for them
  anywhere in this schema (only the function's own name/return type are
  recorded); extending this needs a new extraction step capturing a verbatim
  parameter-list source span, not attempted here.

New unit tests: `tests/unit/lsp/test_references.cpp`, `test_rename.cpp`,
`test_signature_help.cpp` (all three rewritten from their old "always null"
stub tests). Functional tests: `tests/integration/test_07_references.sh`,
`test_11_rename.sh`, `test_12_signature_help.sh` (also rewritten in place, same
numbers) against a new fixture, `fixtures/ref_rename_sighelp.sv`. **A real
fixture-naming bug caught only by the full suite, not the isolated 3-file
run:** the fixture's first draft named its module `adder`, already declared by
`examples/module_basic.sv` and opened by several earlier tests — since this
daemon session shares one `svlsp` process and one growing in-memory DB across
every `test_*.sh` file (never purged on `didClose`), `findSymbolsInScope`
silently unioned both files' ports once `module_basic.sv` had been opened
earlier in the full run. Fixed by prefixing every declared name with `rrsh_`,
matching this repo's own established convention (`gapb_`, `diagvis_`,
`svlsp_test_` prefixes elsewhere) for exactly this reason — see plan.md §6.22
for the full writeup. Unit suite now at 2029 assertions / 614 test cases;
Emacs integration suite now 204/204 across 39 files. No regressions anywhere.
Not yet committed — only commit when asked.

**Implemented 2026-09-12** — plan.md §6.4, cross-file invalidation, scoped to
`` `include `` edges only (the "Not yet done" #1 item below). New schema-v8
table `file_includes(includer_file_id, included_file_id)` — populated in
`CompilationController::compile` (`src/db/compilation_controller.cpp`) from
the same fully-transitive `includedFiles` list the diagnostics-visibility fix
above already computes, so `SymbolDatabase::includersOf(path)` (the reverse
lookup) needs no recursion. `compile` also gained a `forceRecompile` bool that
skips the content-hash cache-hit check unconditionally — needed because a
dependent file's own text can be unchanged even though something it
`` `include ``s just changed.

`LanguageServer`'s `didSave` handler (`src/lsp/server.cpp`), right after its
existing §6.18 primary recompile, looks up every includer of the saved path
and schedules each on a **second** `ChangeDebouncer` instance,
`m_dependencyRechecker` (reusing §6.8's own class rather than a new queue).
Its callback, `forceRecompileAndPublish`, reads text from `m_store` if open
(disk if not), force-recompiles, and publishes — reusing the
diagnostics-visibility fix's `collectIncludedDiagnostics` helper (factored out
of `compileAndPublish` for this). **Deliberately gated to `didSave`, not
`didChange`** — a widely-`` `include ``d header could have many includers, and
recompiling them synchronously or on every keystroke-pause would either stall
the server or flood it during active typing; save is deliberate and
infrequent. **Never holds `m_dataMutex` across the whole cascade** — each
queued dependent is independently lock-scoped (acquire, recompile one file,
publish, release), so an interactive request waiting on the same lock only
ever waits behind one file's compile time.

**Import edges and instantiation edges are deliberately out of scope for this
pass** — see plan.md §6.4's own "Note" for the full reasoning: there's
currently no diagnostic at all for an unresolved import/declared-type
reference (§6.21 covers that gap), and `LibraryResolver`'s own diagnostics are
already unstable today for an unrelated reason (`replaceDiagnostics` silently
wipes them on any live edit, with nothing to re-add them) that needs fixing
before instantiation-edge invalidation would even be meaningful.

**A testing subtlety worth remembering:** diagnostics attribute to the file a
problem actually occurs in, not to every includer — so breaking an included
file does NOT change an includer's own diagnostic *count* (correctly stays
0). The real proof an includer was force-recompiled is that it re-discovers
the changed file as one of its own includes and republishes *that* file's
diagnostics a second time (a version-less echo, distinct from the changed
file's own client-tracked publish) — an initial test-writing mistake assumed
the includer's own count would change; see plan.md §6.4 for the corrected
design.

New unit tests: `tests/unit/db/test_symbol_database.cpp`
(`replaceFileIncludes`/`includersOf`), `tests/unit/db/test_compilation_controller.cpp`
(edge persistence, `forceRecompile`), `tests/unit/db/test_database.cpp`
(schema v8), and a new full-stack file
`tests/unit/lsp/test_server_cross_file_invalidation.cpp` (real pipe
transport: single-includer propagation, no-propagation-without-save, and a
three-includer fan-out). New functional test
`tests/integration/test_38_cross_file_invalidation.sh` — shadows lsp-mode's
own `lsp-diagnostics-updated-hook` with a counter to prove more than one
diagnostics update fires from a single save (the precise per-file mechanism
is already proven at the unit level; `(lsp-diagnostics)` alone can't
distinguish "recompiled, found nothing new" from "never touched"). Unit
suite now at 2000 assertions / 603 test cases; Emacs integration suite now
202/202 across 39 files. No regressions anywhere. Not yet committed — only
commit when asked.

**Implemented 2026-09-12** — the "LSP diagnostics-visibility gap" (documented below
under "Known gaps" and "Not yet done" #2): `LanguageServer::compileAndPublish`
(`src/lsp/server.cpp`) previously only ever called `publishDiagnostics` for the
primary (`didOpen`ed) URI — every transitively `` `include ``d file's own
diagnostics were already computed and persisted to the DB by
`CompilationController::compile` (`replaceDiagnostics(incFid, ...)` per included
file) but silently never sent to the client. Confirmed this is a self-imposed gap
in this codebase, not an LSP protocol limitation: `textDocument/publishDiagnostics`
takes an arbitrary `uri` and an explicitly optional `version` (`Opt<int>`, spec'd
since 3.15.0) for exactly the "this file was never opened" case — the same
mechanism clangd already uses to surface header diagnostics.

`CompilationController::compile` gained an optional out-param,
`std::vector<std::string>* includedFiles` (default `nullptr`, every pre-existing
call site unaffected): populated with every distinct file this call actually
recompiled, left empty on a cache hit (nothing new to report then — a real but
explicitly out-of-scope residual gap, since an included file changing
independently of the primary file's own content hash still isn't detected; that's
Phase 6.4's job). `compileAndPublish` now reads this list and, for each path,
queries `SymbolDatabase::diagnosticsForFile` and publishes with no version
(`std::nullopt` — `DiagnosticsPublisher::publish`/`buildParams` both changed from
`int version` to `std::optional<int>` for this). URIs for included files reuse
the existing `pathToUri()` helper (`symbol_utils.cpp`) — every real production
call site already uses it uniformly, including for definition/workspace-symbol
results that can point into included files, so no new URI-construction
convention was needed.

**A related, necessary fix surfaced in the same pass, not scope creep:** the
loop persisting included-file data was keyed off `recsByFile`'s own keys, so a
file whose parse produced errors but *zero* records at all (unparseable from the
very first token, not just a bad body partway through) was previously dropped
entirely — never persisted to the DB, not merely unpublished. Added a second
pass over `errsByFile` for exactly this case. Real included files with a bad
body but at least one recognizable declaration (the overwhelmingly common case)
were unaffected either way.

New unit test file `tests/unit/lsp/test_server_diagnostics_visibility.cpp`
(registered in `CMakeLists.txt`): drives a real `LanguageServer` over a real
pipe transport (same harness shape as `test_server_debounce.cpp`), opens a
clean primary `` `include ``ing a broken file, and confirms two independent
`publishDiagnostics` notifications arrive — the primary's own (clean, with a
version) and the included file's own (real errors, no version field at all,
not just a null one). Plus 4 new `CompilationController` unit tests
(`includedFiles` populated on a miss / cleared+empty when nothing included /
empty on a cache hit / the zero-records edge case) and 1 new
`DiagnosticsPublisher` test (`buildParams` with `std::nullopt` leaves `version`
unset). New functional test `tests/integration/test_37_diagnostics_visibility.sh`
against two new fixtures (`diagvis_top.sv`/`diagvis_inc.sv`): confirms
`(lsp-diagnostics)` — a session-wide hash lsp-mode maintains independent of
whether a buffer is open — shows the included file's error even though it was
never opened directly, while the primary's own diagnostics stay clean. Unit
suite now at 1701 assertions / 589 test cases; Emacs integration suite now
201/201 across 38 files. No regressions anywhere. Commits: not yet committed —
only commit when asked.

**A process trap hit (and worth flagging for next time) verifying this fix:**
`cmake --build --preset debug --target svlsp_lib` (or `--target unit_tests`)
does **not** relink the standalone `svlsp` executable target — the actual
binary `tools/emacs-test-daemon.sh` runs (`SVLSP_BIN=build/debug/svlsp`) — since
`svlsp` isn't a dependency of either. The first full functional-suite run after
this fix landed passed 200/200 with the *new* `test_37` case failing exactly as
expected pre-fix, which read as a plausible real bug — but the actual cause was
running against a `build/debug/svlsp` last linked 2026-09-08, four days stale,
predating this session's `didSave` work too. Always `cmake --build --preset
debug --target svlsp` (the executable itself, not just `svlsp_lib`) before
trusting an Emacs functional-test result — the same general shape of trap
`handoff.md` already documented once for `tools/build.sh --output-dir` and a
shadowing `~/bin/svlsp` (2026-09-08 entry, further below).

**Implemented 2026-09-12** — plan.md §6.18, recompile on `textDocument/didSave`,
following its own sketch exactly (see plan.md §6.18 for the full writeup). A new
`lsp::notifications::TextDocument_DidSave` handler in `registerHandlers()`
(`src/lsp/server.cpp`, just ahead of the existing `didClose` handler) cancels any
pending debounce entry for the URI (`m_debouncer.cancel`, same call `didClose`
already makes) then calls `compileAndPublish(uri)` synchronously — giving an
unconditional fresh compile at the moment of save regardless of where in §6.8's
300ms debounce window the save lands, closing the gap where a paste-then-save (or
a save-on-focus-loss binding) could briefly show pre-edit diagnostics. No
`CompilationController`/DB changes — this only changes *when* `compileAndPublish`
runs. `DidSaveTextDocumentParams::text` is unused (the server never requested
`includeText`; `compileAndPublish` already reads current text from `m_store`).

Extended `tests/unit/lsp/test_server_debounce.cpp` with a new case plus a
`TestClient::didSave`/`hasPendingMessage` (non-blocking `poll()`) helper pair:
schedules a debounced `didChange` introducing an error, sends `didSave`
immediately, asserts the publish reflects the post-edit text and arrives in
under 250ms (well under the 300ms debounce), then confirms no second, stale
publish follows once the original debounce deadline passes — proving `didSave`
actually cancelled the timer, not merely raced it. New functional test
`tests/integration/test_36_recompile_on_save.sh` (2 cases) against a scratch
temp `.sv` file outside `SVLSP_ROOT` (isolated single-file workspace, since this
test actually calls `save-buffer`): insert-then-save shows the new diagnostic
within 250ms; fix-then-save clears it just as promptly. **A client-side timing
gotcha found writing it:** lsp-mode's own `lsp-idle-delay` (default 0.5s, longer
than the server's 300ms debounce) gates how long lsp-mode waits before even
*sending* a buffered `didChange` — must be lowered (`setq lsp-idle-delay 0.01`)
*before* the buffer's `(lsp)` connection is established (the idle timer captures
the delay at creation time; setting it on an already-connected buffer is a
no-op), or "insert then save immediately" never gets `didChange` onto the wire
in time regardless of whether the server-side fix works. Real end-to-end
latency for the fixed path, confirmed empirically: ~200-220ms.

Unit suite now at 1660 assertions / 583 test cases, no regressions. Commits:
`1e7e17e` (feat + tests), `5b7a888` (docs). **Correction, discovered later the
same session:** the 199/199 Emacs integration re-run reported here at the time
was actually against a `build/debug/svlsp` that had never been rebuilt this
session (only `svlsp_lib`/`unit_tests` had) — see the diagnostics-visibility
entry above for the full story of how that surfaced. `test_36` was re-verified
against a genuinely fresh binary immediately after (still passing, this time
for real) once that was caught.

**Added 2026-09-08** — a follow-on to the `ProjectManifestParser` fix directly below:
after fixing that bug, the user rebuilt their own `svlsp` via `tools/build.sh release`
and re-ran `--build-db` against their already-populated `uvm.db`, and it "exits
immediately" while `` `uvm_fatal `` still failed. Root-caused to a *second*, independent
issue this time (not a stale-binary-on-PATH repeat, though that also happened this same
session — see below): the per-file content-hash cache in `CompilationController` has no
way to know the *parser itself* changed between two `--build-db` runs against the same
output path, only that a file's own text didn't — so a grammar/parser fix (like the
`randomize()`/`super.new()` ones two entries down) never actually gets applied to an
already-built library DB unless the user deletes it first. The user's own proposed fix
— add a version field to the schema, and have `--build-db` check it against the
running binary — is exactly what got implemented.

Schema v7 adds `library_build_info` (`src/db/schema.h`) — a single row recording which
`SVLSP_GIT_VERSION` built a given library DB, mirroring `library_include_dirs`' own
"library DB self-description" shape (schema v6, below). `LibraryDbBuilder::build`
(`src/lsp/library_db_builder.h/.cpp`) gained an optional `currentVersion` parameter —
`main.cpp`'s `--build-db` CLI path passes `SVLSP_GIT_VERSION` (the only place it's
available; baked into the `svlsp` executable target alone, not the whole `svlsp_lib`,
per the `--version` flag's own design note below); every other call site (the live
server's own lazy `ProjectRegistry`-driven `libraryDbSources` path) has no version to
offer and passes `""` unchanged, skipping the check exactly as before this feature
existed. A mismatch — including "nothing recorded", e.g. a DB built before this feature,
or being built into a not-yet-existing output path for the first time — forces a full
rebuild via a new `SymbolDatabase::resetAllFiles()` (`DELETE FROM files`, cascading via
the existing `ON DELETE CASCADE` FKs to symbols/diagnostics/imports/instantiations)
before compiling, rather than trusting the stale per-file hashes. A *matching* version
still reuses the cache exactly as before — this only changes behavior on an actual
mismatch, so re-running `--build-db` twice with the same binary and nothing changed
stays a near-instant no-op (confirmed: 0.02s on the real ~140-file UVM corpus).

13 new unit tests: schema/table creation (`test_database.cpp`); `setBuiltByVersion`/
`builtByVersion` round-trip, overwrite, and "predates this feature" cases plus
`resetAllFiles`' cascade (`test_symbol_database.cpp`); and `LibraryDbBuilder`-level proof
in both directions (`test_library_db_builder.cpp`) — a symbol name planted directly into
the DB that a real recompile of the unchanged source could never produce survives a
same-version rebuild (proving the cache really is reused, not just "happened to produce
the same output") and is discarded by a different-version one (proving the cache really
is bypassed). Full unit suite: 1630 assertions/582 cases, no regressions.

Verified end-to-end against the real motivating `uvm.db`: it had no `library_build_info`
row at all (predates this feature, schema v6). Rebuilding it with a version-checking
binary forced a real ~140-file recompile (not an instant exit — itself proof the
mismatch was detected) and recorded the new version; immediately re-running the
identical command with the same binary completed in 0.02s. `/home/martin/src/policy/
policy_mixin.sv` stayed diagnostic-free throughout. Commit: `c386080`.

**Also found and fixed in the same session** (unrelated code, same investigation): the
"exits immediately" symptom on the *first* attempt (before the version-field feature
existed to explain the second one) turned out to be `/home/martin/bin/svlsp` — first on
the user's own `PATH` — silently shadowing the just-rebuilt `build/release/svlsp` with a
stale pre-fix copy from an earlier `tools/build.sh release --output-dir ~/bin` run.
`tools/build.sh --output-dir` only refreshes that copy when explicitly re-passed; a
plain `tools/build.sh release` (no `--output-dir`) never touches it. Not a code bug —
flagged here since it's an easy trap for this project's own documented `--output-dir`
workflow (see "Build and test" below) to fall into silently. A live `~/bin/svlsp`
process (the user's own already-running editor LSP session, from before the refresh) was
also found holding that file open (`cp`'s destination-file-open fails with "Text file
busy" against a running executable) — refreshing that copy while the old process is
still alive needs a rename-based swap (`mv` a freshly-built copy into place, not `cp`
in place) rather than killing the user's live session; deferred to the user's own next
editor restart rather than done unilaterally.

**Fixed 2026-09-08** — a real user bug report, found right after rebuilding their own
`uvm.db` with the previous day's `randomize()`/`super.new()` grammar fixes (below):
the rebuild itself "immediately finished" (near-instant, as if nothing was compiled),
and their referencing project (`/home/martin/src/policy/policy_mixin.sv`) still failed
every `` `uvm_fatal ``/`` `uvm_error ``-family macro invocation afterward. Root cause:
`ProjectManifestParser::parse` (`src/lsp/project_manifest_parser.cpp`) fell back to the
*literal string* `"."` as `baseDir` whenever the config path had no directory component
at all (`fs::path(path).parent_path()` empty) — exactly what happens running `svlsp
--build-db .svlsp.json --output uvm.db` from inside the manifest's own directory, a
completely natural workflow. Every relative `includeDirs`/`files`/`libraryDirs`/
`libraryFiles`/`libraryDbs` entry then resolved to a bare, un-anchored relative path
instead of an absolute one. Harmless for a single live compile (the process's own CWD
never changes mid-run) but wrong once persisted: `--build-db` bakes `includeDirs` into
the output `.db`'s `library_include_dirs` table (plan.md §6.19 piece 4, 2026-09-07) for
a *different* process — another project's own LSP server, with a different CWD — to
read back later. A literal `"."` baked in this way resolves against whatever *that*
later process's CWD happens to be, not the library's own directory — explaining both
symptoms: the "immediate finish" (if invoked from yet another CWD, `config.files`
entries resolve just as wrongly, so every listed source file is silently skipped —
`ProjectCompiler::loadProject`'s own "missing files silently skipped" behavior — leaving
0 files to compile), and the still-failing macros (the referencing project's own server
attaches the library DB and gets back a useless include dir).

`FilelistParser::parse` already handles this exact empty-baseDir case correctly
(`std::string base = baseDir.empty() ? fs::current_path().string() : baseDir;` —
`src/compiler/filelist_parser.cpp`), so this was a pre-existing asymmetry between the
two config-format parsers (the same general category of bug as the earlier-documented
`ProjectRegistry`-must-pass-explicit-baseDir asymmetry, "Multi-file project support"
below, just manifesting on the JSON-manifest side this time). Fixed by making
`ProjectManifestParser` match that same convention (`fs::current_path().string()`
instead of the literal `"."`). 1 new regression test
(`tests/unit/lsp/test_project_manifest_parser.cpp`): changes CWD, parses a bare filename
with no directory component, confirms the resolved `includeDirs` entry is absolute.
Full unit suite: 1604 assertions/571 cases, no regressions.

Verified end-to-end against the real motivating case: rebuilt the real `uvm.db` in
place with the fixed binary (confirmed `library_include_dirs` now stores the correct
absolute path, not `"."`), then re-ran the live server against the real
`policy_mixin.sv` — previously erroring on every `` `uvm_fatal ``/`` `uvm_error ``
invocation — which now compiles with **zero diagnostics**. Commit: `ece74c5`.

**Fixed 2026-09-08** — two more real grammar bugs found investigating the same
`policy_mixin.sv` file (independent of the fix above): a class method literally named
`randomize()` failed to parse (`randomize_call`'s bare `'randomize'` literal shadowed
`IDENTIFIER` for that spelling everywhere, same root-cause shape as the earlier
`sample()` collision — fixed the same way, rewiring it to plain `IDENTIFIER`); and
`super.new(args)` only parsed as a constructor's literal first statement, never after an
ordinary statement (`class_constructor_declaration` hardcoded the LRM-strict ordering —
relaxed to accept `super.new(...)` anywhere among a constructor's own statements,
deliberately more permissive than the LRM, matching real simulator behavior). See "Sv.g4
grammar quirks" below for the full writeup. 4 new unit tests
(`tests/unit/compiler/test_sv_parser.cpp`, `[randomize]`/`[superctor]` tags). Verified
against the real file (both previously-erroring lines now diagnostic-free) and a full
from-scratch UVM-corpus rebuild (1 diagnostic total, unchanged from the pre-existing
baseline). Commits: `ac20eff` (fix + tests), `774cf64` (docs).

Also on 2026-09-06: `--build-db` gained a growing progress counter — a single
in-place `svlsp: compiling... N/total files` line on stderr (via a new
`ProgressCounterBuf : std::streambuf` in `src/main.cpp` that intercepts the
existing per-file `[parsed] ...` log lines rather than requiring any change to
`LibraryDbBuilder`/`CompilationController`/`ProjectCompiler`), replacing the old
scrolling per-file log for that mode only — `--log-files` (the live server's own
logging) is untouched. `total` seeds from the project's explicit file count and
grows as `` `include ``s/`-v`/`-y` library resolution discover more files, so it
can legitimately hit "100%" more than once before the real total settles. Same
session, a real CLI-parsing bug was found and fixed: `--build-db`/`--output`/
`--log-files` each now require their value to be the literal next `argv` entry —
previously `--build-db --output <db> <config>` (flags grouped before their own
values) silently consumed the literal string `"--output"` as `<config-path>`,
leaving the real db path/config unparsed and failing later with a misleading
"`--build-db` requires `--output`" error even though `--output` was right there;
a bare trailing `--build-db` with nothing after it was also silently ignored,
falling through to the normal stdio server loop instead of erroring. Commits:
`9916094`/`5c613f1` (progress counter + docs), `7494f32`/`49d4279` (CLI-parsing
fix + docs).

**Root-caused and fixed 2026-09-07** — the user's own question ("if everything
is `` `include ``d from one file, does the counter only appear once the whole
build is done?") named the actual bug directly, after an initial investigation
pass (below, kept for the record) failed to reproduce it with the wrong-shaped
test project.

*Investigation, could not reproduce with the wrong project shape:* a release
binary built via `tools/build.sh release --output-dir <dir>`, run as
`<dir>/svlsp --build-db <config> --output <db>`, reportedly showed no
`svlsp: compiling... N/M files` line at all. Tried against `tests/integration/
fixtures/full_project/.svlsp.f` (4 separate top-level files, only one with a
single `` `include ``) in both release and debug `--output-dir` builds, stderr
alone and merged with stdout, and a synthetic `-y`-library-heavy `.f` — the
counter appeared correctly in every case. Also confirmed no `isatty` check
exists anywhere (`grep isatty src/` — no hits) and, at the time, no
`--version`/build-identifying flag existed — since fixed, see "`--version`
flag" below.

*Actual root cause, found via `strace -tt -e trace=write`:* `CompilationController::
compile()` (`src/db/compilation_controller.cpp`) logs the primary file's own
`"[parsed] <path>"` line immediately, but its `"[parsed]   included: <path>"`
lines were only logged in a loop *after* `SvPreprocessor::process` +
`SvTreeWalker::walk` had already fully recursively expanded and parsed the
entire `` `include `` tree in one synchronous call — i.e. only once the whole
top-level compile unit was already done. For a project like the fixture above
(several separate top-level files in `config.files`), this is barely
noticeable: each top-level file still gives a visible tick. But the user's
real UVM `--build-db` config has exactly one top-level file (`uvm_pkg.sv`)
that `` `include ``s everything else — so the *entire* build was one single
`compile()` call: the counter showed `1/1` almost instantly, then **froze
solid for the whole parse** (proven via `strace`: a 424ms gap between the
first and second progress `write()` on even the tiny 2-file fixture, scaling
to however long the real ~140-file parse takes), then every included file's
line fired in a microsecond-scale burst right before the final `svlsp: built
...` summary erased the counter. Watching in real time, that looks exactly
like "the counter never appeared" — nothing to do with release vs. debug or a
stale binary at all.

*Fix:* moved the `"[parsed]   included: <path>"` logging out of
`CompilationController`'s post-hoc `recsByFile` loop and into
`SvPreprocessor::processInclude` (`src/compiler/sv_preprocessor.cpp`) itself —
logged the moment each `` `include `` is actually resolved and opened, before
recursing into it, not after the whole top-level file finishes.
`SvPreprocessor::process` gained an optional `std::ostream* progressLog =
nullptr` parameter (`src/compiler/sv_preprocessor.h`); `CompilationController::
compile` now passes its own `m_log` straight through
(`preprocessor.process(stripped.source, path, m_log)`), so `--build-db`'s
counter and `--log-files` both get the exact same real-time signal for free,
with zero format change to the log lines themselves — `ProgressCounterBuf`
(`src/main.cpp`) needed **no changes at all**, since it already just counts
distinct `"[parsed]"`/`"included:"`-prefixed lines as they arrive. A cache hit
on the primary file is unaffected (that path never calls the preprocessor at
all, same as before); a raw `-v`-file index scan
(`LibraryResolver::declaredTypeNames`) also unaffected (still calls
`process()` with no `progressLog`, matching its pre-existing silent
behavior). One incidental improvement: an included file that declares nothing
(e.g. a macros-only `.svh`, previously invisible to `--log-files` since the
old logging was keyed off `recsByFile`, which only has entries for files with
at least one parse record) is now logged too, since the new logging point
doesn't depend on what the file declares.

Verified end-to-end against a synthetic worst-case shape (one top-level file
`` `include ``ing 30 leaf files) with `strace -tt`: the counter now ticks
`1/1 → 2/2 → ... → 30/30` incrementally as each include is resolved, instead
of sitting at `1/1` until a final burst. 4 new `SvPreprocessor` unit tests
(real-time logging on include resolution; nested-include ordering; no line on
a missing/failed include; unchanged behavior when `progressLog` is omitted)
plus 1 new `CompilationController` regression test (its own `logStream` still
sees both the primary and included lines after the relocation). Unit suite
now at 1587 assertions / 558 test cases, no regressions. No Emacs functional
test needed — this only affects `--build-db`/`--log-files`' own stderr/file
logging, not any LSP-protocol-visible behavior. `docs/usage.md`'s `--build-db`
section gained one clarifying sentence about per-`` `include ``, real-time
counting. Commits: `0cd53c3` (fix + unit tests), `307740b` (docs).

**`--version` flag, added 2026-09-07** (`CMakeLists.txt`, `src/main.cpp`):
closes the gap the investigation above flagged — no way to confirm which
commit a given `--output-dir` copy was actually built from. `CMakeLists.txt`
resolves `git describe --always --dirty --abbrev=7` once at configure time
(this repo has no tags, so it's always just the abbreviated commit hash, with
a `-dirty` suffix if tracked files had uncommitted changes) into
`SVLSP_GIT_VERSION`, baked into the `svlsp` target only (not the whole
`svlsp_lib`, so a commit change doesn't force a full relink of anything else)
alongside `SVLSP_VERSION` (`PROJECT_VERSION`, currently `0.1.0`) and
`SVLSP_BUILD_TYPE` (`$<CONFIG>` generator expression — resolves to
`Debug`/`Release` regardless of preset). `main()` checks for `--version` in
its own pass ahead of every other flag, so it short-circuits unconditionally
even if combined with other flags: `svlsp --version` → `svlsp 0.1.0 (git
307740b, Release build)`. Since `tools/build.sh` always reconfigures from
scratch, this is never stale relative to what it's reporting on. Not unit
tested (CLI-only glue in `main.cpp`, same "not part of any linkable library"
reasoning as `ProgressCounterBuf` and `buildDb()` above) — verified manually
against both debug and release builds instead. `docs/usage.md` gained a new
`--version` section. Not yet committed — only commit when asked.

Also on 2026-09-05: added `tests/unit/lsp/test_completion_latency.cpp` — a
timing-only comparison of `CompletionProvider::getCompletion` with fuzzy matching
(plan.md §6.11) enabled vs. disabled, over 5000 top-level symbols sharing a common
"sig_" prefix (so both modes keep the whole candidate set — the timing difference is
purely each mode's own scoring/sorting overhead, not how many survive). Tagged
`[.]` (Catch2's "hidden" convention) so it's excluded from the default `unit_tests`
run and can't flake normal CI on a noisy machine; run explicitly via
`unit_tests "[completion-latency]"`. Not a correctness test — `test_completion.cpp`'s
own fuzzy-toggle cases already cover that both paths return the right items — and
not a strict fuzzy-vs-strict regression gate either, since the fuzzy path doing
strictly more per-candidate work than the disabled path is expected, not a bug; it
only asserts a generous per-call upper bound (2s) to catch a genuine pathological
blowup (e.g. an accidental O(n²) sort comparator). Measured once for reference:
release build ~11.7ms/call fuzzy-enabled vs. ~9.9ms/call fuzzy-disabled (~18%
overhead, consistent with fuzzy scoring+sorting every candidate vs. a plain
strict-prefix filter with no re-sort); debug/ASan build ~15x slower for both
(~170ms vs. ~147ms), consistent with this project's own documented 10-100x
ASan/UBSan slowdown on real workloads.

On 2026-09-05: added `tests/unit/lsp/test_completion_library_attach.cpp` (2 new test
cases, 16 assertions), closing the one gap §6.19's original three pieces deliberately
left open — every prior library-DB test either checked `SymbolDatabase` queries
directly (`test_symbol_database_library_attach.cpp`) or `LibraryDbBuilder`/
`ProjectRegistry` plumbing with a single attached DB; none drove
`CompletionProvider::getCompletion` itself against **two** independently built and
attached library DBs at once. New coverage: two real, separately-built `.db` files
(via `LibraryDbBuilder::build`, not hand-inserted rows) each defining one class with
one distinctly-named method, both attached via `config.libraryDbs`; dot-completion on
an object of each class resolves to that library's own method and not the other's
(proving no cross-DB leakage); a fuzzy top-level prefix reaches each library's class
name directly. Every case compiles the project's own file **twice** — an initial,
no-usage compile (the `didOpen` equivalent), then a second, edited compile (the
`didChange`/live-edit equivalent, following `live_edit_completion.sv`'s own
"// probe: ..." convention) — and only ever calls `getCompletion` after the second
compile, never the first, since the first compile's content has no library-class
usage for completion to resolve against. Unit suite now at 1578 assertions / 553 test
cases. No regressions.

On 2026-09-04: plan.md gained §6.18 (recompile on `textDocument/didSave` — not yet
implemented, just planned) and §6.11 (configurable fuzzy-matching toggle) was
implemented — see "Configurable fuzzy-matching toggle" under "LSP feature providers"
below and "Not yet done" #13. Also on 2026-09-04: plan.md gained §6.19 (pre-built
library database, attach-and-query shape), and all three of its pieces were
implemented same-day: the standalone `svlsp --build-db`/`LibraryDbBuilder` CLI mode;
referencing an already-built library DB directly via `libraryDbs` (attached
read-only and unioned into `findSymbolsByName`/`findSymbolsByNamePrefix`/
`findSymbolsInScope`/`findSymbolsVisibleAt`); and `libraryDbSources`
(build-and-cache a library DB on first use, from another project's own
config) — see "Database layer" below. `docs/usage.md` (new) documents
`fuzzyCompletion`, `--log-files`, `--build-db`, `libraryDbs`, and `libraryDbSources`
for end users. Piece 3's implementation also surfaced and fixed a real bug in piece 1
(`LibraryDbBuilder::build` didn't create a not-yet-existing output directory, and its
try/catch didn't cover that failure — silently dropped when reached via a
`didOpen` notification; see "Lazy build-and-cache library DBs" below). Unit suite
was at 1562 assertions / 551 test cases as of that date; Emacs integration suite
unchanged at 197 cases / 36 files (none of
§6.19's three pieces change the LSP-protocol surface a client observes beyond
symbols simply being present, already exercised structurally by existing
hover/definition/completion tests — see plan.md §6.19's own note on this). No
regressions anywhere.

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
*separate* bug found in the same investigation (dot-completion can never
resolve into a class declared inside a package) was fixed same-day as
§6.17 — see "Dot-completion into a package-nested class's members" below.
Together, §6.16+§6.17 fully fix the user's original bug report end-to-end
(re-verified directly against their real files), even before they've
fixed an unrelated `` `import ``-typo still present in one of them. Unit
suite now at 1454 assertions / 514 test cases, all green (debug); Emacs
integration suite gained `test_28_keyword_completion.sh` (8 cases),
`test_29_builtin_method_completion.sh` (9 cases),
`test_30_chained_dot_completion.sh` (5 cases),
`test_31_queue_element_completion.sh` (7 cases),
`test_32_live_edit_completion.sh` (3 cases),
`test_33_prototype_methods.sh` (3 cases), and
`test_34_package_scoped_dot_completion.sh` (2 cases), no regressions
anywhere (194/194 total). Working tree has uncommitted changes for §6.17
as of this writing (everything through §6.16 is committed) — not yet
committed, only commit when asked.

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
                   library_resolver, project_compiler, schema — SQLite persistence (schema v8)
src/main.cpp       entry point (supports `--log-files <path>`, see below)
tests/unit/        Catch2 unit tests (614 cases, 2029 assertions)
tests/integration/ Emacs functional test scripts (204 test cases across 39 files)
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
# tools/build.sh [debug|release] [--target NAME] [--output-dir DIR], defaults
# to debug/everything
tools/build.sh                             # from-scratch debug build, everything
tools/build.sh release                     # from-scratch release build, everything
tools/build.sh debug --target svlsp        # from-scratch debug build, svlsp only
tools/build.sh release --output-dir ~/bin  # svlsp only, copied to ~/bin/svlsp --
                                            # a preset's own binaryDir (build/<preset>,
                                            # CMakePresets.json) is fixed and can't be
                                            # redirected on the command line, so this
                                            # builds normally and copies the binary out
tools/build.sh --help                      # full usage

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

# Pre-build a library DB from a .svlsp.json/.f config, then exit (plan.md §6.19 piece 1)
build/release/svlsp --build-db /path/to/uvm.f --output /path/to/uvm.db
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
  `version` is `std::optional<int>` (fixed 2026-09-12, was a plain `int`) — pass
  `std::nullopt` for a file the client never `didOpen`ed, matching the LSP spec's
  own optional `version` field. **Fixed 2026-09-12** (see the entry at the top of
  this document): `LanguageServer::compileAndPublish` now also publishes
  diagnostics for every transitively-included file a compile touched, read back
  from the DB via `SymbolDatabase::diagnosticsForFile` and published with no
  version attached.
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
| `references.h/.cpp` | `ReferencesProvider` | `wordAtPosition` → `findSymbolsByName` (fail closed) → lexical cross-file `findIdentifierOccurrences` scan → `Location[]` (plan.md §6.22) |
| `rename.h/.cpp` | `RenameProvider` | Same scan as references, always including the declaration → `WorkspaceEdit`; throws on an invalid new identifier (plan.md §6.22) |
| `signature_help.h/.cpp` | `SignatureHelpProvider` | Module/interface/program instantiation port lists only (existing `Port` symbols) — backward paren scan finds the enclosing call, named-connection-aware active-parameter tracking; `nullptr` for function/task calls (no per-parameter data exists for those) (plan.md §6.22) |

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
`buildCompletionItems(candidates, prefix, fuzzyEnabled)` helper operating on a source-agnostic
`Candidate{name, kind, detail}` struct (added 2026-09-02 for keyword completion below —
previously took `vector<SymbolRow>` directly). The dot-completion branch (§6.10/§6.13/§6.14
below) builds its own `Candidate`s via `candidatesForResolvedType` rather than a
`vector<SymbolRow>`-taking overload — there's only ever one candidate-building path now,
regardless of chain length (see "Chained/function-call dot-completion" below).

**Configurable fuzzy-matching toggle** (plan.md §6.11, implemented
2026-09-04): `ServerState::fuzzyCompletionEnabled()`
(`src/lsp/server_state.h/.cpp`) reads
`initializationOptions.svlsp.fuzzyCompletion`, defaulting to `true`
whenever it's absent/not-an-object/not-a-boolean — same
absent/wrong-type-means-default shape as `explicitProjectConfigPath`,
resolved once in `handleInitialize` and fixed for the server's lifetime
(no `workspace/didChangeConfiguration` handling exists in this codebase).
`CompletionProvider::getCompletion` gained a `bool fuzzyEnabled = true`
parameter (default preserves every pre-existing call site/test); the
server's own `textDocument/completion` handler (`src/lsp/server.cpp`)
passes `m_state.fuzzyCompletionEnabled()` through.

The disabled path lives inside `buildCompletionItems` itself, not a
separate function: with `fuzzyEnabled=false` and a non-empty prefix, a
candidate survives only a strict, case-sensitive
`compare(0, prefix.size(), prefix) != 0` rejection (not `fuzzyScore`), the
`std::sort` re-rank is skipped entirely (candidate/DB order preserved),
and no `sortText` is assigned — matching §3.6's own documented description
of the pre-fuzzy-matching behavior. An empty prefix is unaffected either
way (every candidate stays in, unranked, regardless of the flag).

**A non-obvious subtlety found while writing the disabled-mode unit
tests:** for any candidate set that actually survives a strict-prefix
filter, fuzzy-enabled ranking degenerates to the *same* relative order
disabled mode already preserves — a full literal-prefix match scores
identically for every survivor, so fuzzy's own score-tie tie-break (name,
ascending) is all that's left distinguishing them, which coincides with
plain alphabetical order for same-scope-depth candidates. Proving disabled
mode truly preserves *DB* order (not just an order indistinguishable from
fuzzy's tie-break) needed a candidate pair at two different scope depths,
since `findSymbolsVisibleAt`'s own SQL-side sort (deepest scope first,
name second) and fuzzy's flat score-then-name sort only genuinely disagree
once scope depth differs.

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
"Dot-completion into a package-nested class's members" directly below for
a *separate*, pre-existing bug found in the same investigation that also
needed fixing before the user's exact real-world case worked end-to-end.

**Dot-completion into a package-nested class's members** (plan.md §6.17,
implemented 2026-09-03, same day found): a real, separate bug uncovered
while building §6.16's functional test. `findSymbolsInScope(scope)`
(`src/db/symbol_database.cpp`) does an exact `s.scope = ?` match against
the fully-qualified scope chain (e.g. `"policy_pkg::PolicyImpl"`), but a
class-typed `Signal`/`Parameter`'s own `detail` — what dot-completion
resolution feeds into it — comes from `userTypeName()`
(`src/compiler/sv_tree_walker.cpp`), which by design "already never
produces a qualified/`::`-containing name" (a §6.10-era simplification,
previously harmless). For any class declared inside a `package`, that
bare `detail` could therefore never match, so resolution always fell
through to "not a real class" — real class members were silently
invisible to `.` completion. Confirmed directly on a real project file
before fixing: a class extending `uvm_object`, declared inside a package,
offered only the synthetic `randomize`-family methods, never its own real
method. Every dot-completion fixture in this repo happened to declare its
classes at top level (bare name and scope chain trivially coincide
there), which is why this went uncaught despite essentially every real
UVM/verification class living inside a package.

Fixed with one new helper, `qualifiedClassScope(db, className, curPath)`
(`src/lsp/completion.cpp`): finds the class's own DB row by name
(`findSymbolsByName`, filtered to `kind == "Class"`), disambiguates with
`pickBestSymbol` (the same same-file-then-first-row logic hover/
definition/`super.` already use), then returns `row.scope.empty() ?
row.name : row.scope + "::" + row.name` — a top-level class reproduces
its bare name unchanged, so every pre-existing test kept passing with
zero fixture changes. Applied at **both** places that previously fed an
unqualified name straight into `findSymbolsInScope`: `candidatesForResolvedType`
(the terminal hop of a chain) and `resolveMemberSegment` (every
*intermediate* hop — easy to miss, since a package-nested class can be
reached either way, e.g. the `child` in `obj.child.greet`). `this.`/
`super.` needed no separate fix: `enclosingClassNameAt`'s own query is
`SELECT s.name` (never qualified), so whatever bare name it hands back
flows into one of the two now-fixed call sites like any other segment.

**A real simplification fell out of the fix, not just the fix itself:**
`candidatesForResolvedType` used to run a *second*, independent
`findSymbolsByName(detail)` scan purely to gate the randomize-family
union on "does this name a real Class". `qualifiedClassScope` returning
non-empty already proves exactly that, so the separate scan was deleted.

Re-verified against the user's real, original bug report
(`all_queue[i].get_policy`, `/home/martin/src/policy/policy_mixin.sv`):
now completes to `get_policy` correctly — combined with §6.16, this fixes
the user's exact original repro completely, even before they've fixed the
unrelated `` `import `` typo still present in that file (its parse-error
cascade turned out not to disrupt `all_queue`'s own scope tracking after
all).

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

### Schema v8 (`src/db/schema.h`, `db::SCHEMA_VERSION = 8`)

- `files (id, path UNIQUE, content_hash, parsed_at)`
- `symbols (id, file_id, kind, name, line, col, parent, detail, end_line, scope)` —
  indexed on name, file_id, scope, (scope,name), (file_id,line,end_line)
- `diagnostics (id, file_id, line, col, message)`
- `imports (id, file_id, pkg_name, item, is_export)` — `item="*"` = wildcard,
  `is_export=1` = `export pkg::item`/`export pkg::*`
- `instantiations (id, file_id, type_name, inst_name, line)` — one row per
  module/interface/program instantiation; drives library resolution
- `library_include_dirs (id, ordinal, dir)` — populated only by
  `LibraryDbBuilder::build` (plan.md §6.19 piece 4, below), never by the
  live server's own `:memory:` DB; a library DB's own `includeDirs` at build
  time, baked into the file itself
- `library_build_info (id, svlsp_version)` — single row, populated only by
  `LibraryDbBuilder::build` (2026-09-08, see top of this document), never by
  the live server's own `:memory:` DB; which `SVLSP_GIT_VERSION` built this
  DB, checked against the running binary on the next `--build-db` to detect
  and force past a stale per-file content-hash cache
- `file_includes (id, includer_file_id, included_file_id)` — one row per file
  transitively `` `include ``d by a given compile, populated on every real
  recompile (plan.md §6.4, 2026-09-12); `SymbolDatabase::includersOf(path)`
  reverse-queries it to find every file needing a forced recompile when
  `path` itself changes

Migrations (`database.cpp`, run automatically): v1→v2 adds end_line/scope; v2→v3 adds
`imports`; v3→v4 adds `imports.is_export`; v4→v5 adds `instantiations`; v5→v6 adds
`library_include_dirs`; v6→v7 adds `library_build_info`; v7→v8 adds `file_includes`.

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
| `setLibraryIncludeDirs(dirs)` | overwrites `library_include_dirs` with `dirs`, in order (plan.md §6.19 piece 4) |
| `libraryIncludeDirs()` | reads `library_include_dirs` back, in order; `{}` if the table doesn't exist (a DB built before this feature) or nothing was stored — never throws |
| `setBuiltByVersion(version)` | overwrites `library_build_info` with a single row holding `version` |
| `builtByVersion()` | reads it back; `""` if the table doesn't exist (a DB built before this feature) or nothing was stored — never throws |
| `resetAllFiles()` | `DELETE FROM files`, cascading (via existing `ON DELETE CASCADE` FKs) to symbols/diagnostics/imports/instantiations — forces every subsequent `compile()` call to be a cache miss |
| `replaceFileIncludes(fileId, includedPaths)` | DELETE + INSERT `file_includes` rows for `fileId` as includer (plan.md §6.4) — called on every real recompile, never a cache hit |
| `includersOf(path)` | reverse lookup: every file whose own compiled unit `` `include ``s `path` — drives forced background recompilation on `didSave` |

`SymbolRow { id, kind, name, line, col, parent, detail, filePath, endLine, scope }`.

### `CompilationController` (`src/db/compilation_controller.h/.cpp`)

`compile(path, text, const ProjectConfig* config = nullptr)` — hash check → skip or
recompile → update DB. `config` seeds `SvPreprocessor`'s include dirs/defines; `nullptr`
(default) preserves pre-multi-file-project behavior exactly. Optional
`std::ostream* logStream` ctor param (default `nullptr`): when set, logs `[parsed] <path>`
(primary; ` (cached)` on a cache hit) itself, and passes the same stream straight through
to `SvPreprocessor::process` as its own `progressLog` — which logs
`[parsed]   included: <path>` in real time, the moment each `` `include `` is actually
resolved, not after this whole `compile()` call returns (see "Real-time `include`
progress logging" fix, 2026-09-07, below) — backs both `--log-files` and `--build-db`'s
progress counter.

### Library dependency graph

```
svlsp_compiler (compiler_directive_stripper, sv_preprocessor, sv_tree_walker,
                parse_cache, filelist_parser, project_config, file_utils)
    → svlsp_antlr4

svlsp_db (database, symbol_database, compilation_controller, library_resolver, project_compiler)
    → svlsp_sqlite3 → svlsp_compiler

svlsp_lib (lsp/*, incl. project_manifest_parser, project_registry, library_db_builder
           — needs lsp::json, not available to svlsp_compiler)
    → lsp → svlsp_compiler → svlsp_db
```

### `LibraryDbBuilder` (`src/lsp/library_db_builder.h/.cpp`, plan.md §6.19 piece 1 — implemented 2026-09-04)

Backs `svlsp --build-db <config-path> --output <db-path>` (`src/main.cpp`):
compiles a `.svlsp.json`/`.f` config into a persistent, file-backed
`Database` and exits, instead of entering the normal `initialize`/stdio
server loop — lets a large, rarely-changing library (UVM, VIP) be
pre-compiled once, independent of any editor session. Dispatches
`configPath` to `ProjectManifestParser`/`FilelistParser` by extension
(`.ends_with(".json")`, C++20's own method — not a duplicate of
`ProjectRegistry`'s private `endsWith` helper), then calls the existing
`ProjectCompiler::loadProject` against a `Database` opened on a real path
(the constructor already supported this; nothing new at that layer).
Returns `Result{ok, fileCount, diagnosticCount, error}` — `diagnosticCount`
via one ad hoc `SELECT COUNT(*) FROM diagnostics` through `Database::prepare`
(already public; no new `SymbolDatabase` query method needed for a
CLI-only summary count) — so `main.cpp`'s own `buildDb()` wrapper is just
argument parsing plus printing that summary (or the error) to stderr.
Factored out of `main.cpp` specifically so it's unit-testable
(`main.cpp` isn't part of any linkable library) — `main.cpp` no longer
touches `ProjectManifestParser`/`FilelistParser`/`ProjectCompiler` directly
at all.

### Attaching a prebuilt library DB (plan.md §6.19 piece 2 — implemented 2026-09-04)

`ProjectConfig` gained `libraryDbs` (`std::vector<std::string>`), populated
from a new `"libraryDbs"` array key in `.svlsp.json`
(`ProjectManifestParser`) or a new chainable `-svlsp_library_db <path>`
switch in `.f` (`FilelistParser` — deliberately not a `+libdb+`-style
spelling, since `.f` is nominally simulator-portable and an unprefixed
`+switch+` risks colliding with a real vendor switch later). Both resolve
relative paths the same way `libraryFiles`/`includeDirs` already do.

`SymbolDatabase::attachLibraryDbs(paths)` (`src/db/symbol_database.h/.cpp`)
`ATTACH DATABASE`s each not-already-attached path read-only under a
generated `lib0`/`lib1`/... alias (a path attached twice — e.g. two
discovered projects sharing one library — is skipped the second time, not
re-attached, which would otherwise double-count its rows). Called from
`ProjectCompiler::loadProject` before compiling any files, so both the live
server and `--build-db` get this uniformly.

Four `SymbolDatabase` queries now `UNION ALL` across every attached schema:
`findSymbolsByName`, `findSymbolsByNamePrefix`, and `findSymbolsInScope`
share a new private `queryAcrossAttachedDbs(cond, bindValue)` helper (one
`WHERE` clause, run against the main schema plus each `lib<N>` schema,
combined results re-sorted in C++ afterward — generalizing
`findSymbolsVisibleAt`'s own pre-existing "SQLite doesn't allow expression
`ORDER BY` after `UNION ALL`" precedent to every query here, not just that
one); `findSymbolsVisibleAt` itself grew its own per-schema arm generation
for Part 2 (cross-file top-level + wildcard-imported scopes) and Part 3
(specific imports), since its shape has multiple distinct `WHERE` clauses
per part rather than one shared one. Its Part 1 (the local scope chain at
the cursor's own position) deliberately stays main-schema-only, since a
cursor is never inside a read-only attached library file.

**Disclosed limitations (see `attachLibraryDbs`'s own doc comment):**
`scopeAtPosition`/`scopeKindAtPosition`/`enclosingClassNameAt` (same
"cursor never in a library file" reasoning), `unresolvedInstantiatedTypeNames`/
`instantiationsOfType` (library content reused this way is expected to be
`import`'d, not instantiated as a design unit), `export pkg::*` re-export
chains starting *inside* an attached DB, and `symbolsForFile`/
`diagnosticsForFile` (opening a library file directly via a followed
definition link won't show its own outline/diagnostics) are all
main-schema-only, not extended in this pass. Also inherits (not introduces)
this codebase's existing session-wide symbol visibility: once any project
attaches a library DB, its symbols are visible to every file/project in
that server session, matching how the main schema already has no
per-project isolation.

Verified via `tests/unit/db/test_symbol_database_library_attach.cpp` (8
cases: each extended query against a real, separately-built, file-backed
library DB; a wildcard-import reaching into an attached package; the dedup
guard; two distinct attached DBs both visible; and an end-to-end
`ProjectCompiler::loadProject` test) plus new cases in
`test_filelist_parser.cpp`/`test_project_manifest_parser.cpp` for the
config-parsing side. No new Emacs functional test (same reasoning as piece
1 — nothing here changes the LSP-protocol surface beyond symbols simply
being present, already exercised structurally by existing hover/definition/
completion tests).

### Lazy build-and-cache library DBs (plan.md §6.19 piece 3 — implemented 2026-09-04)

`ProjectConfig` gained a second, parallel list to `libraryDbs`:
`libraryDbSources` (`std::vector<LibraryDbSource>`, `{configPath,
cachePath}`), populated from `"libraryDbSources": [{"config": ..., "cache":
...}]` in `.svlsp.json` (`ProjectManifestParser`, a new
`readLibraryDbSources` helper — an array of objects, unlike every other
field here) or a new two-argument `-svlsp_library_db_source <config>
<cache>` switch in `.f` (`FilelistParser`, `needArg` called twice).

`LibraryDbBuilder::resolveLibraryDbSources(config, progressLog)`
(`src/lsp/library_db_builder.h/.cpp`, alongside piece 1's `build()`): for
each entry, builds `cachePath` from `configPath` via `build()` only if
`cachePath` doesn't already exist, then appends `cachePath` to
`config.libraryDbs` either way — reusing piece 2's attach-and-query
machinery with zero changes of its own, literally subsuming it as
designed. Called from two places: inside `build()` itself (so a library
being built can transitively depend on further lazily-cached libraries —
`build()` and `resolveLibraryDbSources` call each other, terminating as
long as the dependency graph has no cycle), and from
`ProjectRegistry::loadAndCache` (the live server's own entry point).

**Disclosed, not fixed:** no cycle detection between `libraryDbSources`
chains (unlike `FilelistParser`'s own `-f`/`-F` active-recursion-stack
guard) — a real but contrived gap, needing two or more separately authored
configs coordinated into a cycle to hit.

Verified via new cases in `test_filelist_parser.cpp`/
`test_project_manifest_parser.cpp` (config parsing, including malformed
shapes) and `test_library_db_builder.cpp` (build-on-miss, reuse-on-hit —
proven by pre-populating the cache with a symbol a real rebuild would never
produce and confirming it survives untouched — a failing nested build
propagating as an exception, and `build()`'s own transitive resolution);
plus a `test_project_registry.cpp` case exercising the real live-server
path (`configFor` on a project with an uncached `libraryDbSources` entry
lazily builds it and its symbols become visible). One test-writing mistake
worth remembering for anyone touching this area again: an early draft of
the transitive-resolution test asserted the *built* top-level DB itself
would contain the nested library's symbols when reopened fresh — wrong,
since `ATTACH` is a live, per-connection relationship that's never
persisted into the output file (that's the entire point of attach-and-query
over a physical merge); the correct check is against the nested cache
file's own fresh connection, not the top-level output. No Emacs functional
test, same reasoning as pieces 1/2.

**A real bug found only by hand-driving a live server session, after every
unit test above already passed:** `Database`'s constructor
(`sqlite3_open`) doesn't create missing parent directories, and `build()`'s
try/catch originally covered only config parsing, not the `Database
db(outputPath)` call itself. Every unit test's `cachePath` happened to sit
in a directory some other fixture file already created — this only
surfaced against a `cachePath` whose own directory (modeled on
`docs/usage.md`'s own `/var/cache/svlsp/...` example) didn't exist yet.
Because `resolveLibraryDbSources` is reached from a plain
`textDocument/didOpen` *notification*, lsp-framework's dispatcher silently
drops any exception escaping a notification handler (no response exists to
attach an error to) — the failure produced no error, no log line, nothing,
just a file whose symbols/diagnostics silently never got compiled. The same
missing try/catch would also have made `--build-db` itself crash the whole
process outright for the same case (`main.cpp`'s wrapper has no try/catch
of its own). Fixed by widening `build()`'s try/catch to its whole body and
calling `create_directories` on `outputPath`'s parent first (guarded
against an empty parent — a bare `"out.db"` with no directory component,
which `create_directories` throws on rather than no-op'ing). Two new
regression tests in `test_library_db_builder.cpp`.

### Baking a library DB's own includeDirs into itself (plan.md §6.19 piece 4 — implemented 2026-09-07)

A real user bug report (`` `include "uvm_macros.svh" `` and every
`` `uvm_fatal ``/etc. invocation failing in `/home/martin/src/policy/policy_mixin.sv`,
a project referencing a prebuilt `uvm.db` via `libraryDbs`): a library DB
only ever stored *compiled symbols*, never macro bodies or raw source, so
referencing one couldn't help a project's own `` `include ``s of that
library's macro headers resolve — a real, structural gap, not something
`libraryDbs` was ever going to fix on its own, since macro expansion has to
happen before parsing, from real source text, not from a database of
already-parsed results.

**First design considered, not implemented:** look for a `.f`/`.svlsp.json`
sitting in the same directory as the referenced `.db` file (the config the
library was itself built from) and adopt its `includeDirs`. **Rejected by
the user before implementation**, for a concrete reason: they keep multiple
versions of a library DB in one directory (e.g. archiving a known-good `.db`
before a change) — a directory-sidecar config is necessarily shared/ambient
across every `.db` in that directory, so an old, archived `.db` would
silently pick up whatever the *current* sidecar config says, not what it was
actually built with; and if that config changes incompatibly, the archived
`.db` should keep working with its own original settings, not break or
silently drift.

**Implemented instead: bake the includeDirs directly into the `.db` file.**
New table, schema v6: `library_include_dirs (id, ordinal, dir)` — see
"Schema v6" above. `SymbolDatabase::setLibraryIncludeDirs(dirs)`/
`libraryIncludeDirs()` write/read it (ordinal preserves original search
order). `LibraryDbBuilder::build` calls `setLibraryIncludeDirs(config.includeDirs)`
right after `ProjectCompiler::loadProject` finishes — by that point
`config.includeDirs` already reflects anything *this* config's own
`libraryDbs`/`libraryDbSources` contributed (see below), so a project
attaching just the top-level DB later inherits the full transitive closure,
not only that config's own top-level dirs.

On the read side, `ProjectCompiler::loadProject` (`src/db/project_compiler.cpp`)
gained a new private `mergeIncludeDirsFromLibraryDbs(config)`, called right
after `attachLibraryDbs`, before compiling any file in `config.files` —
satisfying "must run before the preprocessor sees any file" exactly as
requested. For each path in `config.libraryDbs`, it opens its own
standalone, throwaway `Database`/`SymbolDatabase` directly on that file
(deliberately **not** through the live connection's attached-schema
mechanism — that's for symbol queries, unrelated to this) and merges
whatever `libraryIncludeDirs()` returns into `config.includeDirs`, deduped.
Because this only needs `Database`/`SymbolDatabase` (already available in
`svlsp_db`), not `ProjectManifestParser`'s JSON parsing (only available
higher up, in `svlsp_lib` — see "Library dependency graph" above), it lives
in `ProjectCompiler` itself, one call site, rather than needing to be
duplicated at both of `LibraryDbBuilder::build`'s and
`ProjectRegistry::loadAndCache`'s own call sites the way `resolveLibraryDbSources`
is — an earlier draft of this design (a directory-sidecar lookup, before the
rejection above) *did* need exactly that duplication, purely because
`ProjectManifestParser` isn't reachable from `ProjectCompiler`'s own layer;
switching to a DB-embedded table removed that constraint entirely as a side
effect, not just the versioning problem it was chosen to fix.

**Deliberately doesn't call `Database::initSchema()` when reading** a
referenced library DB — only a fresh `sqlite_master` existence check for
`library_include_dirs` before selecting from it (`{}` if absent, e.g. a
`.db` built before this feature existed). Reading must never migrate or
otherwise mutate a file the caller may be treating as an immutable, archived
version — the exact property this whole feature exists to preserve.
Best-effort throughout: a missing `.db` file, or one that fails to open, is
silently skipped (`attachLibraryDbs` already throws a real error for that
case separately; this merge step must not turn it into a second,
differently-worded failure). Only `includeDirs` are adopted this way, not
`defines` — a library built with its own defines (UVM's usual
`UVM_NO_DPI`) may still need those set explicitly in the referencing
project's own config for macro expansion to match exactly how the library
itself was compiled; not attempted here.

**Verified end-to-end against the real motivating bug report**, not just
unit tests: rebuilt a throwaway copy of the user's real `uvm.db` with the
new binary (their actual `uvm.db` was left untouched), confirmed
`library_include_dirs` was populated correctly (`python3 -c
"import sqlite3; ..."` — no `sqlite3` CLI available in this environment),
then ran the live server against the user's real `policy_mixin.sv` two ways
via a scratch `initializationOptions.svlsp.projectConfig` (so the user's own
`.svlsp.json` was never modified): with the new baked-in dirs, every
`` `uvm_fatal ``-related error is gone; with a copy of the same test DB with
`library_include_dirs` cleared (negative control), the exact `` `uvm_fatal(...) ``
parse errors reappear — proving the fix is what's actually responsible, not
some other already-working path. `ProjectCompiler::loadProject`'s own
`config` parameter changed from `const ProjectConfig&` to `ProjectConfig&`
(mutated in place by the merge) — checked every caller (`LibraryDbBuilder::build`,
`ProjectRegistry::loadAndCache`, and every unit test) already held a
non-`const` local, so this was a safe, non-breaking signature change.

8 new unit tests: `SymbolDatabase` set/get round-trip, overwrite, empty-list
clears, and the "predates this feature" `{}` case (`test_symbol_database.cpp`);
schema creates the new table (`test_database.cpp`); `LibraryDbBuilder::build`
actually bakes in the resolved dirs, reopened fresh (`test_library_db_builder.cpp`);
and a full `ProjectCompiler::loadProject` end-to-end case — a real,
separately-built library DB with its own stored dir resolving a `` `include ``
in the referencing project's own file (`test_project_compiler.cpp`). Unit
suite now at 1598 assertions / 566 test cases, no regressions. Not yet
committed — only commit when asked.

**Also surfaced, independent of this fix — two new Sv.g4 grammar quirks**,
found while narrowing the same real bug report down to its actual cause
(the `` `uvm_fatal ``/include errors were masking these underneath): see
"Sv.g4 grammar quirks" below — a method literally named `randomize()` and
`super.new(args)` positioned after another statement in a constructor both
fail to parse. Neither is fixed yet.

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
  own parent dir — **for a bare `path` with no directory component at all** (empty
  `parent_path()`, e.g. invoked as just `.svlsp.json` from inside its own directory),
  this now resolves against the real `fs::current_path()` (fixed 2026-09-08, see top of
  this document — previously fell back to the literal string `"."`, silently wrong once
  persisted into a `--build-db` output's `library_include_dirs` and read back later by a
  different process with a different CWD), matching `FilelistParser::parse`'s own
  already-correct convention for the same empty-baseDir case.
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
| Database | SQLite3 (amalgamation, schema v8) |
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
| Method literally named `randomize()` (`function void randomize(); ... endfunction`) — a common, legal UVM idiom: every class implicitly has a `randomize` method (§6.13's own `RANDOMIZE_METHODS`), and overriding it is normal, not exotic | **Fixed 2026-09-08.** Found investigating a real user bug report (`/home/martin/src/policy/policy_mixin.sv`, which does exactly this at line 69). Same root-cause shape as the already-fixed `sample()` quirk above — `grammar/Sv.g4:3097`'s built-in `randomize_call` rule used the bare string literal `` 'randomize' ``, which ANTLR promotes to its own implicit keyword-like token, so the lexer never offered `IDENTIFIER` for that spelling anywhere else in the file, including as a `function_body_declaration`'s own method name. | **Fixed** — `randomize_call`'s `'randomize'` literal rewired to plain `IDENTIFIER`, the exact fix shape used for `sample()`. Checked for new ambiguity: `randomize_call` sits alongside `tf_call` in several shared alternatives (`subroutine_call`, `constant_primary`, `primary`, `built_in_method_call`); no listener anywhere keys off the `Randomize_callContext` parse-tree type specifically (confirmed via grep before changing), so ANTLR's ambiguity-resolution tiebreak (first-listed alternative wins) is harmless here — a plain call like `foo()` still resolves via `tf_call`/`method_identifier` as before, and only the `with {...}` constraint-block shape (which only `randomize_call` can express) forces that alternative. 2 new unit tests (`tests/unit/compiler/test_sv_parser.cpp`, `[randomize]` tag): a class method named `randomize()` parses clean; existing `randomize()`/`std::randomize()`/`randomize() with {...}` call shapes still parse clean. |
| `super.new(args);` anywhere in a constructor body *except* as the literal first statement (e.g. after an `if` block doing argument validation — another real pattern in the same file, line 32: a fatal-check `if` before `super.new(name)`) | **Fixed 2026-09-08.** Found in the same investigation. `class_constructor_declaration` (`grammar/Sv.g4:522`) hardcoded the LRM's strict structural position: `block_item_declaration* ('super' '.' 'new' (...))? function_statement_or_null*` — i.e. `super.new(...)` could only follow declarations, never an ordinary statement. IEEE 1800-2017 does technically require `super.new` to be the constructor's first statement, but real simulators are commonly more permissive, and this is real, existing UVM-adjacent code. | **Fixed, deliberately more permissive than the LRM** — the fixed position was replaced with a new `class_constructor_body_item*` list (`'super' '.' 'new' (...)? ';' | function_statement_or_null`), allowing `super.new(...)` anywhere among the constructor's own statements (not validated to occur at most once, or first — real simulators' own leniency was the explicit reason to relax this, so no attempt was made to re-impose a narrower rule than "anywhere a statement is legal"). No listener keys off the old fixed super.new slot specifically (confirmed via grep), so this was a pure grammar-shape change. 2 new unit tests (`[superctor]` tag): `super.new(...)` after a preceding `if` statement now parses clean; the original LRM-strict "first statement" placement still parses clean too. |

**Verification for both fixes above:** live-tested against the real motivating file (`/home/martin/src/policy/policy_mixin.sv`) over a real `initialize`/`didOpen` stdio session — both previously-erroring lines (32's `super.new(name)` after an `if` block, and 65-67's `function void randomize(); super.randomize(); endfunction`) now produce zero diagnostics there. Also re-ran this project's own standard real-world check, a full from-scratch `--build-db` over the actual ~170-file UVM corpus (`/home/martin/src/verilator_test/uvm-core/src/.svlsp.json`, release build): **1 diagnostic total**, unchanged from the pre-existing baseline (the already-documented `data_type`/`variable_decl_assignment` ambiguity, `base/uvm_transaction.svh` — see that row above) — confirming these two fixes introduced no new corpus-wide diagnostics or grammar ambiguity regressions.

---

## Known gaps / things to watch out for

- `handleExit` doesn't distinguish clean vs. abnormal exit code; `run()` always returns 0.
- `m_parentProcessId` stored but unused (reserved for parent-process monitoring).
- Emacs test harness needs a display (`Xvfb :99 &; DISPLAY=:99 make test-integration`).
- `CompilationController` uses `":memory:"` SQLite — symbols lost on server restart
  (swap to a file-backed path in `server.cpp` for persistence, straightforward change).
- `export *::*;` LRM shorthand not implemented (see Phase 6.3 section above).
- ~~**References, rename, signature help are unconditional-null stubs.**~~ —
  **implemented 2026-09-13**, each scoped to an honest v1 (lexical cross-file
  text search for references/rename; module/interface/program instantiation
  ports only for signatureHelp) rather than the full semantic version — see
  the entry at the top of this document and plan.md §6.22.
- ~~**LSP diagnostics-visibility gap**: only the primary opened file's diagnostics are
  ever `publish()`'d to the client; included files' diagnostics are computed/persisted
  to the DB but never sent.~~ — **fixed 2026-09-12**, see the entry at the top of this
  document.
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
- ~~Dot-completion can never resolve into a class declared inside a
  package~~ — **fixed 2026-09-03 (§6.17)**, same day it was found while
  building §6.16's functional test. See "Dot-completion into a
  package-nested class's members" under "LSP feature providers" below for
  the full design.
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

1. ~~Phase 6.4 — cross-file invalidation / dependency graph~~ — **implemented
   2026-09-12**, scoped to `` `include `` edges only. See the entry at the top
   of this document and plan.md §6.4 for the full writeup, including why
   import edges (§6.21) and instantiation edges are deliberately deferred.
2. ~~LSP diagnostics-visibility gap~~ — **implemented 2026-09-12**: publish diagnostics
   for every file touched by a `compile()` call, not just the primary opened one. See
   the entry at the top of this document for the full writeup.
3. ~~Implement real `references`/`rename`/`signatureHelp`~~ — **implemented
   2026-09-13**, each scoped to an honest v1 rather than the full semantic
   version (references/rename: lexical cross-file text search, not
   scope-aware; signatureHelp: module/interface/program instantiation port
   lists only, not function/task calls). See the entry at the top of this
   document and plan.md §6.22 for the full writeup. **Not done**: no
   `tests/uvm_corpus/test_*_uvm.cpp` coverage was added for these three —
   this sandbox has no access to the external UVM checkout that suite
   depends on; worth adding whenever this is next touched somewhere that
   does.
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
13. ~~Configurable fuzzy-matching toggle (`plan.md §6.11`)~~ — **implemented
    2026-09-04**, following the original sketch essentially as designed.
    `ServerState::fuzzyCompletionEnabled()` reads
    `initializationOptions.svlsp.fuzzyCompletion` (default `true`, same
    absent/non-object/wrong-type-means-default pattern as
    `explicitProjectConfigPath`), resolved once at `initialize`.
    `CompletionProvider::getCompletion` gained a `bool fuzzyEnabled = true`
    parameter (default preserves every pre-existing call site), threaded
    through by `server.cpp`'s completion handler. Disabled mode lives in the
    shared `buildCompletionItems` helper: a strict, case-sensitive prefix
    filter replaces `fuzzyScore`, the re-sort is skipped entirely (DB order
    preserved), and no `sortText` is assigned. See "Configurable
    fuzzy-matching toggle" under "LSP feature providers" above and plan.md
    §6.11 for the full design, including a subtlety found while writing the
    disabled-mode unit tests: for candidates that survive a strict-prefix
    filter, fuzzy-enabled ranking degenerates to the exact same order
    disabled mode already preserves *unless* the candidates differ in scope
    depth (proving the two modes genuinely differ needed a same-file,
    different-scope-depth candidate pair, not just different names).
    Functional test `tests/integration/test_35_fuzzy_completion_toggle.sh`
    proves the flag reaches `CompletionProvider` through a real `initialize`
    handshake by explicitly tearing down and reconnecting the suite's shared
    workspace (`lsp-workspace-shutdown`, not `lsp-workspace-restart` — the
    latter's respawn raced a `textDocument/completion` request sent
    immediately after and intermittently timed out), restoring the default
    (fuzzy-on) state again before the file ends.
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
      **A concrete design for this was drafted 2026-09-04 (plan.md §6.19),
      and all three pieces are now implemented (2026-09-04) — see
      `LibraryDbBuilder`, "Attaching a prebuilt library DB", and "Lazy
      build-and-cache library DBs" under "Database layer" above:** the
      standalone `--build-db` CLI mode; referencing an already-built
      library DB directly via `libraryDbs`; and `libraryDbSources`
      (a source config + a cache path — build-and-cache on first use if the
      cache path doesn't exist yet, reuse it unconditionally if it does).
      Staleness detection (has the library's own source changed since its
      DB was built/cached) remains an explicitly open question with
      candidate shapes listed, none chosen — see plan.md §6.19 for the full
      writeup, including this pass's other disclosed limitations (no
      `libraryDbSources` cycle detection; several `SymbolDatabase` queries
      deliberately left main-schema-only).
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
20. ~~Dot-completion into a package-nested class's members
    (`plan.md §6.17`)~~ — **implemented 2026-09-03**, same day it was
    found while building §6.16's functional test. See "Dot-completion into
    a package-nested class's members" under "LSP feature providers" above
    for the full design (a new `qualifiedClassScope()` helper in
    `src/lsp/completion.cpp`, applied at both the terminal- and
    intermediate-chain-hop resolution call sites) and plan.md §6.17 for
    the complete writeup. Re-verified directly against the user's
    original bug report end to end: `all_queue[i].get_policy` now
    completes correctly.
21. ~~Recompile on save (`plan.md §6.18`)~~ — **implemented 2026-09-12**,
    following its own sketch exactly. A new `TextDocument_DidSave` handler
    in `registerHandlers()` (`src/lsp/server.cpp`) cancels any pending
    debounce entry (`m_debouncer.cancel`, same call `didClose` already
    makes) and calls `compileAndPublish(uri)` immediately. See the entry at
    the top of this document and plan.md §6.18 for the full writeup,
    including a client-side `lsp-idle-delay` timing gotcha found writing
    the functional test. Recompiling *dependent* files on save remains
    intentionally deferred to §6.4 (cross-file invalidation) once that
    section is planned in file-level detail.
22. ~~Two new grammar quirks, confirmed 2026-09-07~~ — **both fixed
    2026-09-08.** See "Sv.g4 grammar quirks" above for the full writeup on
    each: a method literally named `randomize()` (same root-cause shape as
    the already-fixed `sample()` collision — `randomize_call`'s
    `` 'randomize' `` literal shadowed `IDENTIFIER` for that spelling
    everywhere in the file, fixed by rewiring it to plain `IDENTIFIER`); and
    `super.new(args)` only parsing as a constructor's literal first
    statement, never after an ordinary statement (`class_constructor_declaration`
    baked the LRM-strict ordering directly into the rule's structure — fixed
    by relaxing it, deliberately more permissive than the LRM, to allow
    `super.new(...)` anywhere among a constructor's own statements). Both
    found investigating the same real user bug report
    (`/home/martin/src/policy/policy_mixin.sv`), independent of and
    unrelated to that earlier session's actual fix (plan.md §6.19 piece 4,
    baking a library DB's own includeDirs into itself — see "Library
    dependency graph"/"`LibraryDbBuilder`" above). Re-verified end to end
    against that same real file (both previously-erroring lines now produce
    zero diagnostics) and against a full real-world ~170-file UVM corpus
    rebuild (1 diagnostic total, unchanged from the pre-existing baseline —
    no new corpus-wide diagnostics or ambiguity regressions). 4 new unit
    tests (`tests/unit/compiler/test_sv_parser.cpp`, `[randomize]`/
    `[superctor]` tags). Not yet committed — only commit when asked.
23. **Semantic reference-resolution diagnostics (`plan.md §6.21`)** — not
    started. Found researching §6.4 (cross-file invalidation): there is
    currently no diagnostic anywhere that checks whether a declared type
    reference or an imported symbol actually resolves to a real declaration
    — an `import pkg::X` where `X` doesn't exist (or was just removed)
    produces total silence (hover/completion just fail closed), not an
    error. See plan.md §6.21 for the full design (reuses §6.10/§6.17's
    already-built type-resolution machinery, scoped narrowly to declared
    type references, not general expression-level semantic analysis) and
    the §6.4 section above it for why this is a real prerequisite for
    import-edge cross-file invalidation ever being meaningful. Also
    surfaced a related, already-real bug worth fixing regardless: a live
    edit's own `replaceDiagnostics` call silently wipes any diagnostic
    `LibraryResolver` previously appended, since both share one
    undiscriminated `diagnostics` table with no way to clear just one
    source's own rows.
