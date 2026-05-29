# Decision: SystemVerilog Directive Handling Strategy

## Status
In progress — Phase 4.2a (taxonomy); 4.2b–4.2c pending

---

## Background

`Sv.g4` is a pure parser grammar. It expects source that has already had all backtick
directives resolved. IEEE 1800-2017 §22 labels every backtick construct a "compiler
directive", but from an implementation standpoint they divide into two fundamentally
different kinds that require different handling.

---

## Directive Taxonomy

### Pass 1 — Compiler Directive Strip (Phase 4.2b)

These directives change compilation metadata or state but do **not** transform the text
stream. They can be handled by a simple, dependency-free, line-oriented pass.

`__FILE__` and `__LINE__` are also resolved in this pass. Both must be substituted
against the **original** source file before any `include insertion shifts line counts or
any temp buffer obscures the filename. If left for pass 2, `__FILE__` would expand to a
temp-buffer path and `__LINE__` would reflect post-include line numbers — both wrong.

| Directive | Description | Action |
|---|---|---|
| `` `timescale <unit>/<prec> `` | Sets time unit and precision | Strip; record value |
| `` `default_nettype <type> `` | Default net type for implicit wires | Strip; record value |
| `` `celldefine `` | Mark following module as a cell | Strip |
| `` `endcelldefine `` | End cell region | Strip |
| `` `unconnected_drive pull0\|pull1 `` | Drive unconnected ports | Strip |
| `` `nounconnected_drive `` | Cancel `unconnected_drive` | Strip |
| `` `resetall `` | Reset all compiler-directive state to defaults | Strip |
| `` `begin_keywords "version" `` | Select keyword set for following code | Strip (assume SV-2017) |
| `` `end_keywords `` | Restore previous keyword set | Strip |
| `` `pragma `` | Tool-specific hint (Synopsys, Cadence, etc.) | Strip |
| `` `line N "file" level `` | Override source location for diagnostics | Strip |
| `` `__FILE__ `` | Predefined macro — current source filename | Substitute: original file path |
| `` `__LINE__ `` | Predefined macro — current line number | Substitute: decimal line number in original source |

All names confirmed against IEEE 1800-2017 §22. The `delay_mode_*` and
`default_decay_time` / `default_trireg_strength` constructs that appear in some Verilog
tools are **not** IEEE 1800 standard and are out of scope.

### Pass 2 — Preprocessor (Phase 4.2c)

These directives transform the token stream. The ANTLR4 parser must never see them.
They require a proper preprocessor that tracks macro scope and handles recursive
expansion, stringification, and token-pasting.

| Directive | Description |
|---|---|
| `` `define NAME[(args)] body `` | Macro definition (simple and function-like) |
| `` `undef NAME `` | Remove a named macro |
| `` `undefineall `` | Remove all macros |
| `` `ifdef NAME `` | Conditional block if NAME is defined |
| `` `ifndef NAME `` | Conditional block if NAME is not defined |
| `` `elsif NAME `` | Chained conditional |
| `` `else `` | Else branch |
| `` `endif `` | End conditional block |
| `` `include "file" `` | Insert file content into token stream |
| `` `NAME `` (invocation) | Expand a defined macro |

---

## Two-Pass Pipeline

```
original source file
        │
        ▼
┌──────────────────────────────┐
│  Pass 1: CompilerDirective   │  (Phase 4.2b)
│  Stripper                    │
│                              │
│  • strips metadata directives│
│  • `__FILE__ → "orig.sv"    │  ← original path, not a temp file
│  • `__LINE__ → "42"         │  ← line in original source
│  • records DirectiveRecord[] │
└──────────────────────────────┘
        │ cleaned source (no metadata directives)
        ▼
┌──────────────────────────────┐
│  Pass 2: Preprocessor        │  (Phase 4.2c — tool TBD)
│                              │
│  • expands `define macros    │
│  • resolves `ifdef/`ifndef   │
│  • inserts `include files    │
└──────────────────────────────┘
        │ fully preprocessed source
        ▼
   ANTLR4 SvParser
```

---

## Preprocessor Tool Decision (Phase 4.2c)

**Status:** Accepted — Phase 4.2c

### Candidates Evaluated

| Candidate | Language | Licence | Verdict |
|---|---|---|---|
| `slang` preprocessor | C++ | MIT | Preferred external option, but see below |
| `verilator --preproc` | C++ | LGPL | Subprocess-only; LGPL static-linking restrictions |
| `sv-parser` | Rust | MIT | Requires Rust toolchain + C FFI — build complexity |
| **Minimal in-house** | C++ | — | **Selected** |

### Decision

**Use a minimal in-house C++ preprocessor** (`src/compiler/sv_preprocessor.h/.cpp`).

### Rationale

1. **slang** is technically the best external option (MIT, full IEEE 1800-2017,
   `slang::parsing::Preprocessor` is a public embeddable class). However, its preprocessor
   produces a **token stream**, not source text. Reconstructing preprocessed source from
   tokens is lossy (whitespace and comments discarded) and adds significant integration
   complexity. slang also adds a large build-time dependency (~100 KLOC).

2. **verilator** is subprocess-only (no library API) and LGPL adds static-linking
   compliance burden.

3. **sv-parser** requires the Rust toolchain as a hard CMake dependency — not acceptable
   for a C++-only project.

4. **Minimal in-house** covers the directive subset needed for RTL indexing (object-like
   and function-like macros, conditional compilation, file inclusion) with zero extra
   dependencies. The `SvPreprocessor` class provides a stable interface so slang can be
   swapped in later (e.g. for full UVM support) without changing callers.

### Limitations of Current Implementation

- Stringification (`` `" ``) and token-pasting (`` `​`` ``) are not implemented. These are
  used heavily in UVM base macros but are rare in RTL. Record as an error if encountered.
- No line-mapping across `include boundaries (positions in preprocessed output do not map
  back to original source lines for included files).

### Upgrade Path

To add slang: implement `SvPreprocessor` backed by `slang::parsing::Preprocessor`, feed it
the pass-1 output, and collect the token stream. Reconstruct source text by joining token
spellings with their trailing trivia (whitespace). Swap the implementation in CMakeLists by
replacing `src/compiler/sv_preprocessor.cpp` — callers are unchanged.
