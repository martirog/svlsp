# Phase 2 — LSP Server Framework Evaluation and Setup

## What was built

A minimal but real LSP server that Emacs lsp-mode can connect to and
exchange `initialize`/`shutdown` messages with, backed by Catch2 unit tests.

## Architecture

```
src/lsp/server_state.h/.cpp   — Pure state machine (no I/O; unit-testable)
src/lsp/server.h/.cpp         — LanguageServer: wires lsp-framework to ServerState
src/main.cpp                  — Entry point: stdio transport → LanguageServer::run()
```

### Why the split?

`ServerState` has zero dependency on lsp-framework's I/O or threading layer,
making it directly instantiable in unit tests. `LanguageServer` owns the
connection and only lives in the running binary and integration tests.

## State Machine

```
Uninitialized  ──(initialize)──►  Active  ──(shutdown)──►  Shutdown  ──(exit)──►  Inactive
```

- Any request in the wrong state throws `lsp::RequestError` with the correct error code.
- `exit` without prior `shutdown` is allowed (LSP spec §3.4); the process exits with code 1.
- `initialized` (notification) is a no-op; reserved for future cache warm-up.

## How to build

```bash
cmake --preset debug
cmake --build --preset debug
```

lsp-framework's `lspgen` tool is built first and generates `types.h` and
`messages.h` from `lspgen/metaModel.json`. This happens automatically.

## How to run unit tests

```bash
make test-unit
# or directly:
build/debug/unit_tests
```

11 test cases, 18 assertions covering all state transitions and error conditions.

## How to run the server manually

```bash
build/debug/svlsp   # reads LSP JSON-RPC from stdin, writes to stdout
```

Pipe a hand-crafted `initialize` request to verify:

```bash
printf 'Content-Length: 152\r\n\r\n{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"rootUri":null,"capabilities":{}}}' | build/debug/svlsp
```

## Functional tests (Emacs)

`tests/integration/test_02_svlsp_connection.sh`:
- Opens `examples/module_basic.sv` in Emacs with verilog-mode + lsp-mode.
- Waits up to 15 s for svlsp to reach the `initialized` state.
- Verifies the workspace server-id is `svlsp`.

Run:
```bash
bash tools/emacs-test-daemon.sh tests/integration/test_02_svlsp_connection.sh
```

## Dependencies

| Dependency | Version | How added |
|---|---|---|
| lsp-framework | v1.3.1 | `git submodule` at `third_party/lsp-framework` |
| Catch2 | v3.8.1 | `FetchContent` in CMakeLists.txt |

See [docs/decisions/lsp-framework.md](decisions/lsp-framework.md) for the
framework evaluation and rationale.
