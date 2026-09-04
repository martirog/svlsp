# svlsp Usage Guide

This is a quick reference for the user-facing configuration knobs `svlsp`
currently has: the completion fuzzy-matching toggle (set by your
editor/client at connect time), the `--log-files` command-line flag, and the
`--build-db` standalone pre-build mode (both set when you launch the
binary). It will grow into the full end-user guide tracked by `plan.md`
Phase 6.6 as more of that phase lands; for now it only covers what's
actually implemented.

## Configuring the server: `initializationOptions`

`svlsp` reads configuration from the standard LSP `initialize` request's
`initializationOptions`, under an `svlsp` namespace:

```json
{
  "initializationOptions": {
    "svlsp": {
      "fuzzyCompletion": false,
      "projectConfig": "/path/to/.svlsp.json"
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
normal `initialize`/stdio server loop in this mode:

```
[parsed] /path/to/uvm/uvm_pkg.sv
[parsed]   included: /path/to/uvm/uvm_macros.svh
svlsp: built '/path/to/uvm.db' -- 143 files compiled, 0 diagnostics
```

`--build-db` requires `--output`; the reverse (`--output` with no
`--build-db`) is ignored and the server starts normally. A bad or
unreadable `<config-path>` exits with status 1 and an error on stderr,
without creating `<db-path>` at all.

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
one) also won't show its own outline or diagnostics yet. `svlsp` never
checks whether an attached database is stale relative to whatever built
it — see `plan.md` §6.19's own open question on this; for now, rebuild with
`--build-db` and overwrite the file yourself when the library changes.

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

**Additional limitation on top of `libraryDbs`'s own above:** `svlsp` never
checks whether `cache` has gone stale relative to `config`'s own files —
if the library is upgraded in place without also changing (or deleting)
`cache`, the old, cached DB keeps being used. Delete the cache file
yourself to force a rebuild. There's also no cycle detection between
`libraryDbSources` chains (library A's source depending on library B, whose
own source points back at A) — keep dependency configs acyclic.
