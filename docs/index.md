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
- `usage.md` — end-user guide *(added in Phase 6)*

## Project Phases

| Phase | Description | Status |
|---|---|---|
| 0 | Project Infrastructure | In progress |
| 1 | Emacs Daemon Test Infrastructure | Pending |
| 2 | LSP Server Framework Evaluation and Setup | Pending |
| 3 | LSP Feature Implementation | Pending |
| 4 | SystemVerilog ANTLR4 Compiler Front-End | Pending |
| 5 | SQLite Database Layer | Pending |
| 6 | End-to-End Integration and Polishing | Pending |
