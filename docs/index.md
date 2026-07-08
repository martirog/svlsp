# svlsp Documentation

## What is svlsp?

`svlsp` is a SystemVerilog Language Server implemented in C++. It provides IDE features
for SystemVerilog source files — including completions, go-to-definition, hover,
diagnostics, and more — through the Language Server Protocol (LSP).

## Quick Start

### Build

Requires: CMake ≥ 3.20, GCC 13+ (or Clang 19+), Pandoc (for docs).

```bash
cmake --preset debug
cmake --build --preset debug
```

The server binary is placed at `build/debug/svlsp`.

For a release build:

```bash
cmake --preset release
cmake --build --preset release
```

### Run Tests

```bash
make test-unit          # C++ unit tests
make test-integration   # Emacs functional tests (requires Emacs + lsp-mode)
make test               # Both
```

### Build Documentation

```bash
make docs-html   # HTML output in build/docs/
make docs-pdf    # PDF output in build/docs/ (requires pandoc + xelatex)
make docs        # Both
```

## Documentation Index

- [Project Plan](../plan.md) — phased development plan
- [Architecture Decisions](decisions/) — rationale for key technology choices
- [Phase 1 — Emacs Test Infrastructure](phase1-emacs-test-infrastructure.md)
- [Phase 2 — LSP Server Setup](phase2-lsp-server-setup.md)
- [Phase 3 — LSP Feature Implementation](phase3-lsp-features.md)
- [Phase 4 — ANTLR4 Compiler Front-End](phase4-antlr4-compiler.md)
- [Phase 5 — SQLite Database Layer](phase5-sqlite-database.md)
- [Phase 6 — End-to-End Integration and Polishing](phase6-lsp-providers.md)
- `usage.md` — end-user guide *(added in Phase 6.6)*

## Project Phases

| Phase | Description | Status |
|---|---|---|
| 0 | Project Infrastructure | Complete |
| 1 | Emacs Daemon Test Infrastructure | Complete |
| 2 | LSP Server Framework Evaluation and Setup | Complete |
| 3 | LSP Feature Implementation | Complete |
| 4 | SystemVerilog ANTLR4 Compiler Front-End | Complete |
| 5 | SQLite Database Layer | Complete |
| 6 | End-to-End Integration and Polishing | In progress (6.1, 6.3 complete) |
