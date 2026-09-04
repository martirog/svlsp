# svlsp Usage Guide

This is a quick reference for the two user-facing configuration knobs
`svlsp` currently has: the completion fuzzy-matching toggle (set by your
editor/client at connect time) and the `--log-files` command-line flag (set
when you launch the binary). It will grow into the full end-user guide
tracked by `plan.md` Phase 6.6 as more of that phase lands; for now it only
covers what's actually implemented.

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
