# Phase 3 — LSP Feature Implementation

Each sub-phase follows the pattern: unit test → implementation → Emacs functional test → two commits.

---

## 3.1 Text Document Synchronisation

**Status:** Complete

### What was added

`src/lsp/document_store.h/.cpp` — `DocumentStore` class:

| Method | Description |
|---|---|
| `open(DidOpenTextDocumentParams)` | Stores URI → { text, version } |
| `update(DidChangeTextDocumentParams)` | Replaces text and version (Full sync) |
| `close(DidCloseTextDocumentParams)` | Removes the document |
| `contains(uri)` | Returns true if the URI is currently open |
| `get(uri)` | Returns the stored document; throws `std::out_of_range` if unknown |

`LanguageServer` (in `server.h/.cpp`) now owns a `DocumentStore m_store` and registers
three notification handlers:

```
textDocument/didOpen   → DocumentStore::open()
textDocument/didChange → DocumentStore::update()
textDocument/didClose  → DocumentStore::close()
```

The store uses `uri.toString()` as the map key so lookups are O(log n) string comparisons.

### Design notes

- `DocumentStore` is pure business logic with no I/O, making it directly unit-testable
  without mocking the framework.
- The server advertises `TextDocumentSyncKind::Full`, so `contentChanges` always contains a
  single full-text entry.  `update()` uses `std::visit` to extract `.text` from whichever
  `TextDocumentContentChangeEvent` variant arrives, making it robust if the client sends a
  range change regardless.
- Unknown-URI `update` and `close` calls are silent no-ops (the LSP spec discourages but
  does not forbid out-of-order notifications).

### Unit tests

`tests/unit/lsp/test_document_store.cpp` — 9 test cases:

| Test | What it checks |
|---|---|
| initially empty | `contains` returns false before any open |
| open stores document | `contains` returns true after open |
| open stores correct text and version | `get` returns the right content |
| multiple documents can be open | two URIs tracked independently |
| update replaces text and version | `get` reflects new content after change |
| update of unknown URI is a no-op | no crash, no entry created |
| close removes document | `contains` returns false after close |
| close of unknown URI is a no-op | no crash |
| get on unknown URI throws | `std::out_of_range` raised |

Run with: `make test-unit` or `./build/debug/unit_tests`

### Functional test

`tests/integration/test_03_document_sync.sh` — 3 test cases:

| Test | How it works |
|---|---|
| server alive after didOpen | Opens fixture, waits for `initialized`, checks workspace still active |
| server alive after didChange | Opens fixture, inserts a character (triggers `didChange`), waits 2 s, checks workspace |
| server alive after didClose | Opens two fixtures, closes the first (sends `didClose`), verifies second workspace still active |

Run with:
```bash
bash tools/emacs-test-daemon.sh tests/integration/test_03_document_sync.sh
```

### Fixtures added

`examples/module_params.sv` — a parameterised shift-register module, used as the second
fixture in the `didClose` test so closing one buffer does not shut down the server.

---

---

## 3.2 Diagnostics (`textDocument/publishDiagnostics`)

**Status:** Complete (empty push — real parse errors wired in Phase 4)

### What was added

`src/lsp/diagnostics.h/.cpp` — `DiagnosticsPublisher` class:

| Method | Description |
|---|---|
| `publish(uri, version, diags={})` | Sends `publishDiagnostics` via `MessageHandler` |
| `static buildParams(uri, version, diags={})` | Builds params without sending — for unit tests |

`LanguageServer` now owns a `DiagnosticsPublisher m_diagnostics{m_messageHandler}` (declared
after `m_messageHandler` so member initialisation order is safe) and calls
`m_diagnostics.publish(uri, version)` inside the `didOpen` and `didChange` handlers.

### Design notes

- `buildParams` is `static` so it can be unit-tested without a live connection or mock.
- Publishing empty diagnostics on `didOpen`/`didChange` clears any stale client-side
  errors from a previous session, which is correct even before Phase 4 adds parsing.
- `didClose` deliberately does NOT publish diagnostics — lsp-mode automatically clears
  the diagnostic overlay when the buffer is killed.

### Unit tests

`tests/unit/lsp/test_diagnostics.cpp` — 6 test cases covering URI, version, empty default,
explicit empty vector, non-empty diagnostic array, and range preservation.

### Functional test

`tests/integration/test_04_diagnostics.sh` — 2 test cases:

| Test | How it works |
|---|---|
| zero diagnostics after didOpen | Opens fixture, waits for LSP, sit-for 2s, checks `(lsp--get-buffer-diagnostics)` length == 0 |
| zero diagnostics after didChange | Opens fixture, inserts a newline, sit-for 3s, checks length == 0 |

---

## Remaining Phase 3 sub-phases

| Sub-phase | Feature | LSP method | Status |
|---|---|---|---|
| 3.3 | Hover | `textDocument/hover` | Pending |
| 3.4 | Go-to-definition | `textDocument/definition` | Pending |
| 3.5 | Find references | `textDocument/references` | Pending |
| 3.6 | Completion | `textDocument/completion` | Pending |
| 3.7 | Document symbols | `textDocument/documentSymbol` | Pending |
| 3.8 | Workspace symbols | `workspace/symbol` | Pending |
| 3.9 | Rename | `textDocument/rename` | Pending |
| 3.10 | Signature help | `textDocument/signatureHelp` | Pending |
