# svlsp Usage Guide

This is a quick reference for the user-facing configuration knobs `svlsp`
currently has: the completion fuzzy-matching toggle (set by your
editor/client at connect time), and the `--version`, `--log-files`,
`--log-requests`, `--parse-worker-max-rss`, `--parse-worker-idle`,
`--in-process-parse` and
`--build-db` command-line flags (all set when you launch the binary). It
will grow into the full end-user guide tracked by `plan.md` Phase 6.6 as
more of that phase lands; for now it only covers what's actually
implemented.

## Configuring the server: `initializationOptions`

`svlsp` reads configuration from the standard LSP `initialize` request's
`initializationOptions`, under an `svlsp` namespace:

```json
{
  "initializationOptions": {
    "svlsp": {
      "fuzzyCompletion": false,
      "docComments": false,
      "projectConfig": "/path/to/.svlsp.json",
      "database": "/path/to/project.db"
    }
  }
}
```

Every option here is resolved once, when the connection is first
initialized, and stays fixed for the life of that server process — none of
them can be changed on a running connection.

Exactly how you set `initializationOptions` depends on your editor/client.
For Emacs `lsp-mode`, pass a plist (or a zero-argument function returning
one) via `:initialization-options` when registering the client:

```elisp
(lsp-register-client
 (make-lsp-client
  :new-connection (lsp-stdio-connection "svlsp")
  :major-modes     '(verilog-mode)
  :server-id       'svlsp
  :initialization-options
  (lambda () (list :svlsp (list :fuzzyCompletion :json-false)))))
```

**Emacs: raise `gc-cons-threshold`.** lsp-mode's own performance guide
recommends it, and for svlsp it is what makes large completion lists
fast: with Emacs's default 800 KB threshold, parsing a 1000-item
completion response triggers garbage collections of ~30 ms each. Measured
on the UVM library (tests/perf/README.md): the empty-prefix completion
drops from 85 ms to 22 ms through `completion-at-point`, and the ~30-40 ms
spikes on every other request go away.

```elisp
(setq gc-cons-threshold (* 100 1024 1024))
```

`read-process-output-max` (also in that guide) made no measurable
difference here.

### Fuzzy completion matching (`fuzzyCompletion`)

By default, `textDocument/completion` fuzzy-matches your typed prefix
against candidate names: it tolerates typos and skipped characters (e.g.
typing `wdth` still offers `WIDTH`), and ranks results by match quality
(exact/contiguous/word-start matches first).

Set `svlsp.fuzzyCompletion` to `false` to turn this off:

```json
{ "initializationOptions": { "svlsp": { "fuzzyCompletion": false } } }
```

With it disabled, completion instead uses a strict, case-sensitive prefix
match — a candidate is offered only if its name literally starts with what
you typed — and results are returned in the server's own natural order,
unranked (no reordering by match quality).

| | `fuzzyCompletion: true` (default) | `fuzzyCompletion: false` |
|---|---|---|
| Typo/skip-tolerant (`wdth` → `WIDTH`) | Yes | No |
| Match rule | Case-insensitive subsequence | Strict, case-sensitive prefix |
| Result order | Ranked by match quality | Server's natural order |

Any value other than a JSON boolean `false` (missing, wrong type, or
`initializationOptions` absent entirely) leaves fuzzy matching on — you only
need to set this if you want to turn it *off*.

### Doc comments (`docComments`)

A comment block directly above a declaration — no blank line in between —
is recorded as that declaration's documentation, and shown by hover,
signature help and completion (fetched per item via `completionItem/resolve`):

```systemverilog
// Adds two numbers.
function int add(int a, int b);
```

When nothing is directly above, a comment after the declaration on the same
line is used instead (`int count; // number of items`). Separator lines
(`//-----`) and UVM tag lines (`// @uvm-ieee ...`) are dropped; a block made
only of those is no documentation. The same rules apply to `` `define ``s.

Storing the docs costs database space (the in-memory database of a live
session, and a `--build-db` file — for UVM about 190 KB, under 5% of a 4.3 MB
database). Set `svlsp.docComments` to `false` to collect none:

```json
{ "initializationOptions": { "svlsp": { "docComments": false } } }
```

Like `fuzzyCompletion`, anything other than a JSON boolean `false` leaves
it on. It applies to what the server compiles itself; a library database
has its docs decided when it is built (`--no-doc-comments`, below).

## Checking what you actually built: `--version`

```bash
$ svlsp --version
svlsp 0.1.0 (git 307740b, Release build)
```

Prints the project version, the abbreviated commit `tools/build.sh` (or any
`cmake --build`) actually built from, and the build type, then exits
immediately — takes priority over every other flag if combined with any.
`-dirty` is appended to the commit if the source tree had uncommitted changes
to tracked files when it was built (the version is re-read from git on every
build, not only when CMake configures). Answers "is this `--output-dir` copy
actually the binary I think it is" in one command, instead of having to
rebuild from scratch and compare timestamps to rule out a stale copy.

## Logging parsed files: `--log-files`

Pass `--log-files <path>` on the command line to have `svlsp` log every file
it parses or reuses from cache:

```bash
svlsp --log-files /tmp/svlsp-files.log
```

The file is opened in append mode, so restarting the server doesn't erase
earlier runs' logs. Each `didOpen`/`didChange` compile logs the primary file,
plus one line for every `` `include``d file discovered while parsing it:

```
[parsed] /home/user/proj/top.sv
[parsed]   included: /home/user/proj/defines.svh
[parsed] /home/user/proj/top.sv (cached)
```

`(cached)` means the file's content hash hadn't changed since the last
compile, so it was skipped rather than reparsed. This is mainly useful for
confirming a multi-file project's full expected file set is actually being
parsed — e.g. spotting a misconfigured include path that silently leaves a
file out.

## Timing requests: `--log-requests`

Pass `--log-requests <path>` to log how long the server itself spends on
each request and compile (appending, like `--log-files`):

```bash
svlsp --log-requests /tmp/svlsp-requests.log
```

```
[request] compile /home/user/proj/tb.sv wait 0.002 ms, run 812.440 ms
[request] textDocument/completion /home/user/proj/tb.sv:21:13 wait 0.001 ms, run 41.212 ms
[request] completionItem/resolve wait 0.001 ms, run 0.310 ms
```

Positions are the request's own, 0-based. `wait` is the time spent waiting
for another request or a background compile to finish; `run` is the
server's own work, excluding JSON (de)serialization and transport -- so the
latency the editor sees, minus `wait + run`, is transport and client-side
time. `compile` lines cover `didOpen`/`didChange`/`didSave` compiles (the
first one in a project also loads its library DBs and project files);
`recompile` lines are files recompiled because an `` `include``d file
was saved.

## The parse worker: `--parse-worker-max-rss`, `--parse-worker-idle`, `--in-process-parse`

The server parses in a separate process, a second `svlsp` it starts itself
(`svlsp --internal-parse-worker`, visible in `ps`). The parser keeps what
it learns in memory it never gives back: compiling a large unit live (UVM
as project sources, not as a `libraryDbs` database) grows it to ~7 GB.
In the worker that memory is released when the worker is replaced, so the
server's own memory stays small whatever it has compiled.

The worker stays alive between compiles (later parses are faster with
what it has learned). After a compile that leaves it above
`--parse-worker-max-rss <MB>` (default 1024; `0` = never replace it), it
is replaced once no compile has run for `--parse-worker-idle <seconds>`
(default 60; `0` = right after that compile). So while you edit, the
warm worker keeps recompiles fast (UVM compiled live: ~10 s per edit,
~19 s with a fresh worker), and when you stop, the memory goes back. The
peak of a large compile is the same either way; what the limit bounds is
what stays resident in an idle session. A worker that crashes is restarted and
the parse retried once; if that fails too, the file gets one error
diagnostic on line 1 ("the parser worker failed").

`--in-process-parse` parses inside the server instead (the behavior
before the worker). `--build-db` always parses in-process: it is a
one-shot run whose memory goes when it exits. `--log-files` logs each
worker start, each parse's time and size, and each replacement:

```
[worker] started pid 41203
[worker] pid 41203 walk 894581 ms (8650 KB in, 3601 KB out)
[worker] pid 41203 at 6626 MB > 1024 MB, recycled once idle 60 s
[worker] pid 41203 walk 9685 ms (299 KB in, 166 KB out)
[worker] pid 41203 at 6666 MB > 1024 MB, recycled once idle 60 s
[worker] pid 41203 idle 60 s, recycled
```

## Background indexing

A project loads in the background. Opening a file starts its project's load
and returns at once. The load goes in this order:
1. the files that `` `include `` the opened file (its package);
2. the opened file itself;
3. the files declaring what it imports or extends;
4. the rest, in config order.

A request on an open file waits only for that file's own compile, not for
the whole project.

While the project indexes:
- Answers that need other files may be incomplete. Completion says so
  (`isIncomplete`), so the client asks again as you type.
- Diagnostics that depend on other files are held back until the load ends,
  so no false warnings appear. These are unknown declared types, missing
  call arguments and unresolved instantiations. Parse errors are published
  at once.
- Clients that support work-done progress (lsp-mode does) show
  "svlsp: indexing 12/120 units" in the mode line.

The `svlsp.waitForIndexing` command (`workspace/executeCommand`) answers
once no load or refresh is queued or running. It is meant for scripts and
tests.

## Include errors

In a file that belongs to a project, an `` `include `` that svlsp can't
follow is a diagnostic on the directive. It is reported in the file that
holds the directive, even when that file is itself included and never
opened. The squiggle runs from the backtick to the end of the file name.

| Message | Severity | Meaning |
|---|---|---|
| `` `include: cannot find "x.svh" `` | Error | Not beside the including file, and not in any `includeDirs`. |
| `` `include: cycle detected for "/path/a.svh" `` | Error | The file includes itself, directly or through others, with no include guard to stop it. |
| `` `include: depth limit exceeded (cycle?) `` | Error | Includes are nested more than 17 levels deep. |
| `` `include: cannot open "/path/x.svh" `` | Error | The file exists but can't be read. |
| `` `include: missing or malformed filename `` | Warning | No `"name"` or `<name>` after `` `include ``. `` `include `MACRO `` is not supported yet, so it gets this Warning too. |

The rest of the file is still compiled without the included text, as a
simulator would. Symbols that file would have declared are therefore
missing, and their uses may be flagged as well.

A file svlsp compiles on its own, outside any project, gets no include
errors. It has no `includeDirs`, so every library `` `include `` (such as
`uvm_macros.svh`) would be reported. Add the file to a project's `files`,
with the right `includeDirs`, to see them.

The error goes away when the file appears. That happens on the next compile
of the including file, or on a refresh when files change on disk (see
[Branch switches](#branch-switches-file-watching-and-svlsprefresh)). It
comes back when the file is removed.

## A project database built ahead: `database`

A project's database is a file, so it lasts between sessions. By default
it is `$XDG_CACHE_HOME/svlsp/<hash of the config's path>.db` (`~/.cache`
when `XDG_CACHE_HOME` is unset), so nothing appears in your tree, and two
checkouts of one repo get two databases. To choose the file yourself:
- `"database": "project.db"` in `.svlsp.json`, relative to the config;
- `-svlsp_db project.db` in a `.f` filelist;
- `initializationOptions.svlsp.database`, which wins over the config.

`"database": ":memory:"` (or `-svlsp_db :memory:`) keeps the project in
memory, as before: it is compiled from scratch every session. A file
opened outside any project is always compiled into memory.

With a database file:
- The next start reads it and compiles only the files whose contents
  changed. Each file is checked by size and mtime, and its hash only when
  those differ.
- It can be built outside the editor, before you start one, with the same
  command as a library database:

  ```sh
  svlsp --build-db .svlsp.json --output project.db
  ```

  Rerunning it updates the file in place, and only what changed is
  recompiled.

One process writes a database at a time. A lock file, `project.db.lock`,
sits beside it:
- **A `--build-db` is still writing it when the server starts.** The server
  answers from what the build has committed so far. Once the build is done,
  the server takes the file over and refreshes it.
- **Another server has it open.** The second server copies it into memory
  and never writes the file.
- **`--build-db` while a server has it open.** It exits at once ("svlsp pid
  N has it open; it refreshes it itself"). `--wait` makes it wait instead.

Other notes:
- Text you haven't saved is compiled into the database as it is. The next
  start recompiles that file from disk.
- A database written by another svlsp build is reset and rebuilt.
- A server uses one database. A second project with a different
  `database` setting shares the first one, and a message is logged.

## Branch switches: file watching and `svlsp.refresh`

When files change outside the editor, as on a `git checkout`, the server
refreshes the project: it rereads the config and recompiles only the files
whose inputs changed. Files gone from the config or from disk lose their
rows, and the cross-file checks re-run where declarations changed.

The server learns about the change in one of three ways:
- **File watching.** The server asks the client to watch `*.sv`, `*.svh`,
  `*.v`, `*.vh`, `*.svi` and `*.f` files and the config files. The events of
  one checkout are coalesced into one refresh, half a second after they
  stop. Events for files you have open are ignored, because the buffer is
  the truth. lsp-mode watches with file-notify. On a large tree it may ask
  before watching more than `lsp-file-watch-threshold` directories, and
  Linux's `fs.inotify.max_user_watches` may need raising.
- **The `svlsp.refresh` command** (`workspace/executeCommand`) refreshes
  every loaded project. With the argument `"--rehash"` it reads every file
  instead of trusting size and mtime. In Emacs:
  `(lsp-request "workspace/executeCommand" '(:command "svlsp.refresh"))`.
- **A `post-checkout` git hook** keeps a project database fresh with no
  editor open:

  ```sh
  #!/bin/sh
  # .git/hooks/post-checkout
  svlsp --build-db .svlsp.json --output project.db >/dev/null 2>&1 &
  ```

  When a server has the database open, the hook's `--build-db` leaves it to
  the server.

## Pre-building a library database: `--build-db`

For a large, rarely-changing dependency (UVM, verification IP) that's
reused unmodified across many projects, you can compile it once into a
standalone database file instead of paying that compile cost inside every
project's own server session:

```bash
svlsp --build-db /path/to/uvm.f --output /path/to/uvm.db
```

`<config-path>` is either a `.svlsp.json` manifest or a `.f`/`.svlsp.f`
filelist — the same two formats a live project already uses, dispatched by
extension. `svlsp` compiles every file it resolves (including anything
pulled in via `-y`/`-v` library resolution) into a fresh SQLite database at
`<db-path>`, prints a one-line summary, and exits — it never enters the
normal `initialize`/stdio server loop in this mode.

While it works, a single in-place counter on stderr tracks progress —
`compiled/total` files seen so far. The total file count isn't known
upfront (transitive `` `include ``s and -v/-y library resolution both
discover more files as compilation proceeds), so it starts at the
project's own explicit file count and grows to match once that's
exceeded:

```
svlsp: compiling... 87/143 files
svlsp: built '/path/to/uvm.db' -- 143 files compiled, 0 diagnostics
```

Each `` `include ``d file counts the moment it's actually resolved, not only
once its enclosing top-level file finishes compiling — this matters for a
library like UVM whose whole source tree is reached through one top-level
file (`uvm_pkg.sv`) that `` `include ``s everything else: the counter still
ticks up file by file as each one is discovered, rather than sitting at
`1/1` for the entire build and then jumping straight to the final count.

Add `--no-doc-comments` to leave doc comments (see
[`docComments`](#doc-comments-doccomments)) out of the database, for a
smaller file:

```bash
svlsp --build-db /path/to/uvm.f --output /path/to/uvm.db --no-doc-comments
```

**Resources.** Building the full UVM library takes about 14 minutes and
peaks at about 7 GB of memory (one job; don't run several at once on a
small machine). A project that uses the finished database instead is
cheap: a server for a UVM testbench with `libraryDbs` peaks at ~55 MB.
Opening a large library's own sources as a live project (no database)
costs the server the same as building it, and the server keeps that
memory until it exits -- see plan.md §6.33.

The setting is recorded in the database with the building binary's version,
so rebuilding into the same `<db-path>` with the flag added or removed
recompiles every file rather than keeping the cached ones as they were.

`--jobs N` parses N files at once (default 1). Commits stay one at a time,
and they take about half the time on a multi-unit project, so the gain is
small: about 10% on a 120-unit testbench with 4 jobs, for 2.5 times the
memory.

`--build-db` requires `--output`; the reverse (`--output` with no
`--build-db`) is ignored and the server starts normally. A bad or
unreadable `<config-path>` exits with status 1 and an error on stderr,
without creating `<db-path>` at all.

**Each flag's value must be the literal argument immediately following
it** — `--build-db` and `--output` can appear in either order relative to
*each other* (`--build-db <config> --output <db>` and `--output <db>
--build-db <config>` both work), but a flag's own value can't be pushed
past another flag. `--build-db --output <db> <config>` (both flags grouped
before their values) is rejected with a usage error rather than silently
misparsed — it would otherwise consume the literal string `--output` as
`<config-path>`.

## Using a pre-built library database: `libraryDbs`

Once you have a database built with `--build-db`, point your own project's
config at it so its symbols become part of your project's hover/definition/
completion results, without recompiling the library yourself. In
`.svlsp.json`:

```json
{ "files": ["top.sv"], "libraryDbs": ["/path/to/uvm.db"] }
```

Or in a `.f`/`.svlsp.f` filelist, one path per `-svlsp_library_db` (repeat
the switch for more than one library DB):

```
top.sv
-svlsp_library_db /path/to/uvm.db
```

Each path is attached read-only to the project's own database at load time.
Library symbols become visible the same way any other file's top-level
symbols already are — including through a wildcard import
(`import uvm_pkg::*;`) that reaches a package declared entirely inside the
attached database. There's nothing further to configure; hover, go-to-definition,
and completion all pick this up automatically once `libraryDbs` is set.

**Current limitations:** this makes library symbols visible for
name/scope-based lookups (hover, definition, completion), but a module or
interface defined *only* inside an attached library database still won't
resolve if you *instantiate* it directly (library content reused this way
is expected to be `import`'d, not instantiated) — this is a workflow for
sharing packages/classes (UVM-style verification code), not RTL modules.
Opening a library file directly (e.g. by following a definition link into
one) also won't show its own outline or diagnostics yet.

**Stale library databases.** A database built with `--build-db` records
each source file's size, mtime and content hash. Each time the project
loads or refreshes, the server checks an attached `libraryDbs` database
against its files. Only files whose size or mtime changed are read, so
UVM's 166 files take a few milliseconds. svlsp doesn't own this file, so
it never rebuilds it. Instead it warns once per session, with a
`window/showMessage` and on stderr:

```
svlsp: /path/to/uvm.db is stale: 3 of 166 files changed since it was built, e.g. ... (rebuild it with svlsp --build-db)
```

A database built before schema v17 recorded no stat data, so it is
never reported. For CI, `--check` tells without writing anything:

```sh
svlsp --build-db uvm.f --output uvm.db --check   # exit 0 up to date, 1 stale, 2 error
```

## Lazily building and caching a library database: `libraryDbSources`

If you'd rather not run `--build-db` yourself ahead of time, point at the
library's own source config instead of a prebuilt database, and let
`svlsp` build-and-cache it automatically the first time it's needed:

```json
{
  "files": ["top.sv"],
  "libraryDbSources": [
    { "config": "/vip/uvm/uvm.f", "cache": "/var/cache/svlsp/uvm-1.2.db" }
  ]
}
```

Or in a `.f`/`.svlsp.f` filelist:

```
top.sv
-svlsp_library_db_source /vip/uvm/uvm.f /var/cache/svlsp/uvm-1.2.db
```

`config` is another `.svlsp.json`/`.f` describing the library's own files —
exactly what you'd hand to `--build-db` directly. At load time: if `cache`
already exists on disk, it's used as-is (no recompiling); if it doesn't,
`svlsp` builds it from `config` first (the same work `--build-db` does),
writes it to `cache`, and then uses it — so the first project to load pays
the library's compile cost once, and every later load (this project or any
other pointing at the same `cache` path) reuses it for free. From this
point on it behaves exactly like a `libraryDbs` entry pointing at `cache`.

**A stale `cache` is rebuilt.** svlsp owns this file, so when the check
above finds it stale, svlsp rebuilds it in the background before the
project's files compile. The rebuild works on a copy and is incremental:
only the changed files are recompiled. The copy is then renamed into place
and attached again. There's no cycle detection between
`libraryDbSources` chains (library A's source depending on library B, whose
own source points back at A) — keep dependency configs acyclic.
