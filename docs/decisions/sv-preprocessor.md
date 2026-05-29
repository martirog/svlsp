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

## Preprocessor Tool Candidates (Phase 4.2c)

| Candidate | Language | Licence | Notes |
|---|---|---|---|
| `slang` preprocessor | C++ | MIT | Full IEEE 1800-2017; `slang::parsing::Preprocessor` is embeddable as a library |
| `verilator --preproc` | C++ | LGPL | Mature; can be spawned as a filter subprocess |
| `sv-parser` | Rust | MIT | Full SV 2017 with preprocessing; requires a C FFI wrapper |
| Minimal in-house | C++ | — | Handle `` `define/undef/undefineall/ifdef/include `` + invocation |

Evaluation criteria: C++ embeddability, licence compatibility, recursive macro support,
stringification (`` `" ``), token-pasting (`` `​`` `​`` ``), function-like macros with
arguments, `include path resolution.

Tool decision to be recorded here after Phase 4.2c evaluation.
