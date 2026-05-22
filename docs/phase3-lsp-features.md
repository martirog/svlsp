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

## 3.3 Hover (`textDocument/hover`)

**Status:** Complete (always returns null — real symbol info wired in Phase 4)

### What was added

`src/lsp/hover.h/.cpp` — `HoverProvider` class:

| Method | Description |
|---|---|
| `static getHover(HoverParams)` | Returns `null` (`NullOr<Hover>`) — Phase 4 will query the symbol database |

`LanguageServer::registerHandlers()` in `server.cpp` now registers a handler for
`lsp::requests::TextDocument_Hover` that delegates to `HoverProvider::getHover`.

`handleInitialize` in `server_state.cpp` now advertises:

```cpp
.hoverProvider = lsp::OneOf<bool, lsp::HoverOptions>(true)
```

### Design notes

- `HoverProvider::getHover` is `static` — the handler has no I/O side effects and no
  stored state, so it is directly unit-testable without any mock or instance setup.
- The result type `lsp::TextDocument_HoverResult` is `NullOr<Hover>` (`Nullable<Hover>`).
  Returning `nullptr` (the `std::nullptr_t` overload) serialises to JSON `null`, which
  lsp-mode maps to Emacs `nil`.  Editors handle a null hover gracefully — no popup shown.
- `hoverProvider = true` must be advertised in capabilities so that lsp-mode actually sends
  hover requests.  Without it the client silently suppresses them.
- Phase 4 will inject a symbol-database lookup into `getHover` and return a populated
  `lsp::Hover` with `MarkupContent{ .kind = MarkupKind::Markdown, .value = ... }`.

### Unit tests

`tests/unit/lsp/test_hover.cpp` — 3 test cases:

| Test | What it checks |
|---|---|
| returns null at position (0,0) | `result.isNull()` is true at the start of the file |
| returns null at arbitrary position | same at (10, 5) — no position is special |
| returns null for any URI | two distinct files both yield null |

Run with: `make test-unit` or `./build/debug/unit_tests`

### Functional test

`tests/integration/test_05_hover.sh` — 2 test cases:

| Test | How it works |
|---|---|
| server alive after hover request | Opens fixture, waits for LSP, sends `textDocument/hover` via `lsp-request`, verifies workspace still `initialized` |
| hover returns null (pre-ANTLR4) | Same setup, checks `(null result)` is true for the JSON null response |

Run with:
```bash
bash tools/emacs-test-daemon.sh tests/integration/test_05_hover.sh
```

---

---

## 3.4 Go-to-Definition (`textDocument/definition`)

**Status:** Complete (always returns null — real symbol resolution wired in Phase 4)

### What was added

`src/lsp/definition.h/.cpp` — `DefinitionProvider` class:

| Method | Description |
|---|---|
| `static getDefinition(DefinitionParams)` | Returns `null` (`NullOrOneOf<Definition, Array<DefinitionLink>>`) — Phase 4 will resolve symbol locations |

`LanguageServer::registerHandlers()` now registers a handler for
`lsp::requests::TextDocument_Definition` that delegates to
`DefinitionProvider::getDefinition`.

`handleInitialize` now advertises:

```cpp
.definitionProvider = lsp::OneOf<bool, lsp::DefinitionOptions>(true)
```

### Design notes

- The result type `lsp::TextDocument_DefinitionResult` is
  `NullOrOneOf<Definition, Array<DefinitionLink>>` — a `NullableVariant` over
  `std::variant<Definition, Array<DefinitionLink>>`.  Returning `nullptr` serialises to
  JSON `null`; lsp-mode maps that to Emacs `nil` and performs no jump.
- `getDefinition` is `static` for the same reason as `HoverProvider::getHover` — no
  stored state or I/O side effects, so no instance setup is needed in tests.
- Phase 4 will return a `Definition` (i.e. `OneOf<Location, Array<Location>>`) populated
  from the ANTLR4 symbol table and the SQLite source-location index.

### Unit tests

`tests/unit/lsp/test_definition.cpp` — 3 test cases:

| Test | What it checks |
|---|---|
| returns null at position (0,0) | `result.isNull()` is true at the start of the file |
| returns null at arbitrary position | same at (10, 5) — no position is special |
| returns null for any URI | two distinct files both yield null |

Run with: `make test-unit` or `./build/debug/unit_tests`

### Functional test

`tests/integration/test_06_definition.sh` — 2 test cases:

| Test | How it works |
|---|---|
| server alive after definition request | Opens fixture, waits for LSP, sends `textDocument/definition` via `lsp-request`, verifies workspace still `initialized` |
| definition returns null (pre-ANTLR4) | Same setup, checks `(null result)` is true for the JSON null response |

Run with:
```bash
bash tools/emacs-test-daemon.sh tests/integration/test_06_definition.sh
```

---

---

## 3.5 Find References (`textDocument/references`)

**Status:** Complete (always returns null — real reference lists wired in Phase 4)

### What was added

`src/lsp/references.h/.cpp` — `ReferencesProvider` class:

| Method | Description |
|---|---|
| `static getReferences(ReferenceParams)` | Returns `null` (`NullOr<Array<Location>>`) — Phase 4 will query all reference sites from the symbol database |

`LanguageServer::registerHandlers()` now registers a handler for
`lsp::requests::TextDocument_References` that delegates to
`ReferencesProvider::getReferences`.

`handleInitialize` now advertises:

```cpp
.referencesProvider = lsp::OneOf<bool, lsp::ReferenceOptions>(true)
```

### Design notes

- `ReferenceParams` carries a `ReferenceContext` with an `includeDeclaration` bool.
  Phase 4 will use this flag to decide whether to include the declaration site in the
  returned location list alongside usage sites.
- The result type `NullOr<Array<Location>>` is `Nullable<Array<Location>>`.  Returning
  `nullptr` serialises to JSON `null`; lsp-mode maps that to Emacs `nil` and shows an
  empty reference list.
- `getReferences` is `static` — no stored state or I/O side effects at this stage.

### Unit tests

`tests/unit/lsp/test_references.cpp` — 3 test cases:

| Test | What it checks |
|---|---|
| returns null at position (0,0) | `result.isNull()` is true at the start of the file |
| returns null at arbitrary position | same at (10, 5) — no position is special |
| returns null regardless of includeDeclaration | both `false` and `true` yield null |

Run with: `make test-unit` or `./build/debug/unit_tests`

### Functional test

`tests/integration/test_07_references.sh` — 2 test cases:

| Test | How it works |
|---|---|
| server alive after references request | Opens fixture, waits for LSP, sends `textDocument/references` via `lsp-request` (with `includeDeclaration: true`), verifies workspace still `initialized` |
| references returns null (pre-ANTLR4) | Same setup with `includeDeclaration: false`, checks `(null result)` is true |

Run with:
```bash
bash tools/emacs-test-daemon.sh tests/integration/test_07_references.sh
```

---

## Remaining Phase 3 sub-phases

| Sub-phase | Feature | LSP method | Status |
|---|---|---|---|
| 3.3 | Hover | `textDocument/hover` | Complete |
| 3.4 | Go-to-definition | `textDocument/definition` | Complete |
| 3.5 | Find references | `textDocument/references` | Complete |
| 3.6 | Completion | `textDocument/completion` | Pending |
| 3.7 | Document symbols | `textDocument/documentSymbol` | Pending |
| 3.8 | Workspace symbols | `workspace/symbol` | Pending |
| 3.9 | Rename | `textDocument/rename` | Pending |
| 3.10 | Signature help | `textDocument/signatureHelp` | Pending |
