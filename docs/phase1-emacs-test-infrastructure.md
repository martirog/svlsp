# Phase 1 — Emacs Daemon Test Infrastructure

## What was built

A reproducible, headless Emacs test harness that can connect to any LSP server
and verify its behaviour from the client's perspective.

## Components

| File | Purpose |
|---|---|
| `tools/emacs-test-init.el` | Minimal Emacs init loaded by the test daemon |
| `tools/emacs-test-daemon.sh` | Main test runner: installs packages, starts daemon, runs tests |
| `tools/emacs-test-lib.sh` | Shared helpers: `run_test`, `skip_test`, `section`, `print_summary` |
| `tests/integration/test_00_daemon_sanity.sh` | Verifies daemon and lsp-mode setup |
| `tests/integration/test_01_clangd_connection.sh` | Validates harness with clangd + C++ |
| `tests/integration/fixtures/hello.cpp` | C++ fixture file used by the clangd test |

## How it works

1. `emacs-test-daemon.sh` pre-installs `lsp-mode` from MELPA into
   `.emacs-test/elpa/` on first run (stamp file: `.emacs-test/packages-installed`).
   Subsequent runs skip this step and start immediately.
2. An Emacs daemon is started with `--no-init-file`, loading only
   `tools/emacs-test-init.el`. This avoids interference from the developer's
   personal Emacs config.
3. The daemon is assigned a unique name (`svlsp-test-<PID>`) so parallel test
   runs do not collide.
4. The script polls the daemon socket (up to 30 s) then runs each
   `tests/integration/test_*.sh` file in sorted order by sourcing it.
5. Each test file calls `run_test <name> <elisp> <expected>`. The Elisp is
   evaluated in the live daemon via `emacsclient -e` and the result compared
   to the expected string.
6. On exit (normal or error), the daemon is killed via the `cleanup` trap.

## Environment variables

| Variable | Set by | Description |
|---|---|---|
| `SVLSP_ROOT` | `emacs-test-daemon.sh` | Absolute path to the project root |
| `SVLSP_BIN` | `emacs-test-daemon.sh` | Path to `build/debug/svlsp` |
| `DAEMON_NAME` | `emacs-test-daemon.sh` | Socket name for this daemon instance |

## How to run

```bash
# All integration tests
make test-integration

# Single test file
bash tools/emacs-test-daemon.sh tests/integration/test_01_clangd_connection.sh
```

## How to add a new test

1. Create `tests/integration/test_NN_<name>.sh`.
2. Call `section "..."`, `run_test`, and `skip_test` from `emacs-test-lib.sh`.
3. The file is picked up automatically by `emacs-test-daemon.sh` (sorted order).

Example:
```bash
section "My feature"

run_test "something returns t" \
    "(some-elisp-expression)" \
    "t"
```

## Package cache

`lsp-mode` and its dependencies are stored in `.emacs-test/elpa/` which is
listed in `.gitignore`. Delete `.emacs-test/packages-installed` to force a
package reinstall.

## Validating the harness (clangd)

`test_01_clangd_connection.sh` opens `tests/integration/fixtures/hello.cpp`,
connects `clangd-19` as the LSP server, waits for the `initialized` handshake,
and verifies that a hover request returns content for the `greet` function.
This test must pass before any `svlsp`-specific tests are added.
