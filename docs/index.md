# svlsp Documentation

## What is svlsp?

`svlsp` is a SystemVerilog Language Server implemented in C++. It provides IDE features
for SystemVerilog source files — including completions, go-to-definition, hover,
diagnostics, and more — through the Language Server Protocol (LSP).

## Quick Start

### Build

Requires: CMake ≥ 3.24, git, a C++20 compiler (GCC ≥ 10 or Clang ≥ 14 with
a C++20 standard library), `antlr4` 4.13.2 on `PATH` or Java ≥ 11, and the
`third_party/lsp-framework` submodule (`git submodule update --init
--recursive`). Pandoc for the docs.

```bash
tools/build.sh --check     # check all of the above, picks the compiler
tools/build.sh             # from-scratch debug build (CXX=... to choose a compiler)
```

or by hand, naming the compiler on the first configure:

```bash
cmake --preset debug $(tools/find-cxx20.sh)
cmake --build --preset debug
```

The server binary is placed at `build/debug/svlsp`.

For a release build:

```bash
tools/build.sh release     # or: cmake --preset release $(tools/find-cxx20.sh) && cmake --build --preset release
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
- [Usage Guide](usage.md) — configuring `initializationOptions` and `--log-files`
  *(started ahead of Phase 6.6; will grow into the full end-user guide as more of
  that phase lands)*

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
