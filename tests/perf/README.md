# Latency benchmarks

Two harnesses time the same scenarios:

| | runs | measures |
|---|---|---|
| `make perf-lsp` | `tools/lsp_perf.py`: svlsp alone over stdio | the server plus the pipe, to the last response byte |
| `make perf-emacs` | `tests/perf/emacs_perf.sh` in the Emacs test daemon | the same through lsp-mode: JSON parse into Lisp, plus `[capf]` (lsp-mode's completion pipeline) and `[render]` (hover rendering) rows |

Both default to the release build (`PERF_SVLSP=...` to change it) and the
`uvm` scenario (`SCENARIO=small` for the one that needs no corpus). Neither
is part of `make test`.

```sh
make perf-lsp                                   # uvm, 10 runs per request
make perf-lsp SCENARIO=small PERF_ARGS="--repeat 20 --check"
tools/lsp_perf.py tests/perf/uvm.json --only hover --repeat 50
tools/lsp_perf.py tests/perf/uvm.json --type-interval 300   # typing that overlaps compiles
DISPLAY=:99 make perf-emacs
DISPLAY=:99 SVLSP_PERF_SETUP=my-settings.el make perf-emacs  # other lsp-mode settings
```

`--json FILE` (`SVLSP_PERF_JSON` for Emacs) keeps the results;
`tools/lsp_perf.py --report FILE` prints the Emacs ones again. `--check`
exits 1 when a p90 is over its budget.

## Scenarios

`tests/perf/<name>.json`: the workspace (`files` / `copyDir` copied into a
temp dir, `write` for generated manifests with `${VAR}`s from `vars`), the
file to `open`, an optional `ready` probe (completion polled until a label
appears: the library DB loads on the first request), the `requests`, an
`edit` and a `typing` session.

A position is `{"find": text, "sub": text-inside-it, "offset": n | "end"}`:
the first occurrence of `find`, then of `sub` in it, plus `offset`
characters (default 1, inside the word) or just after it.

- `uvm` -- `uvm_perf_tb.sv`, a small testbench on the full UVM library DB
  (`SVLSP_UVM_DB`, default the corpus `uvm.db`).
- `small` -- the 6-file `full_project` integration fixture.

## Columns

`median` / `p90` / `max` are the client's round trip. `srv run` is the
median of the server's own time (its `--log-requests` line: the data lock
held), `srv wait` the longest wait for that lock. A request that queues
behind a `didChange` (whose handler blocks on the lock while a compile
runs) shows that wait in the round trip only. `results` is items /
locations / edits; `bytes` the response size (standalone only).

- *edit -> diagnostics*: from the change to the diagnostics for it. svlsp
  debounces changes 300 ms; lsp-mode also delays full-sync changes 1.0 s
  after the last keystroke (`lsp-debounce-full-sync-notifications-interval`)
  unless a request flushes them first -- with the default headerline
  breadcrumb, its `documentSymbol` request does at `lsp-idle-delay` (0.5 s).
  The Emacs harness makes the idle timers an edit arms count from the edit
  (`svlsp-perf/keystroke-idle`), as a real keystroke does.
- *typing*: a character every `--type-interval` ms (150 ~ 80 wpm), with a
  completion after each, as an auto-popup UI does.

## Budgets

`tests/perf/budgets.json`, judged on the p90:

| tier | budget | requests |
|---|---|---|
| instant | 100 ms | (any) |
| keystroke | 200 ms | completion, resolve, signature help, typing |
| point | 400 ms | hover, definition, document symbols |
| navigation | 1 s | references, rename, workspace symbols |
| diagnostics | 1 s | edit -> diagnostics |

Sources:
- Nielsen's three limits (from Miller 1968 and Card et al. 1991): 0.1 s
  feels instantaneous, 1 s keeps the flow of thought uninterrupted, 10 s
  keeps attention. <https://www.nngroup.com/articles/response-times-3-important-limits/>
- Doherty & Thadani (IBM, 1982): productivity rises sharply below 400 ms.
  <https://lawsofux.com/doherty-threshold/>
- web.dev RAIL: respond to input within 100 ms (process it in 50 ms).
  <https://web.dev/articles/rail>
- gopls: default completion budget 100 ms; under 200 ms is "OK"; 500 ms,
  1 s and 5 s are "landmarks of slowness". <https://github.com/golang/go/issues/63129>
- Typing latency: noticeable from roughly 20-100 ms depending on the user;
  200 ms measurably hurts text correction (Regensburg, MUM '23).
  <https://www.inkandswitch.com/slow-software>,
  <https://epub.uni-regensburg.de/55007/1/text-input-latency.pdf>
