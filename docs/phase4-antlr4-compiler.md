# Phase 4 — SystemVerilog ANTLR4 Compiler Front-End

**Goal:** Parse SystemVerilog source files into an AST using ANTLR4 and extract semantic
information into the database.

Each sub-phase follows the pattern: unit test → implementation → Emacs functional test → two commits.

---

## 4.1 Grammar Integration

**Status:** In progress (tool detection complete; grammar and runtime pending)

### What was added

`cmake/ANTLR4Tool.cmake` — CMake module that locates the ANTLR4 code-generation tool:

| Step | Action |
|---|---|
| 1 | `find_program(ANTLR4_EXECUTABLE antlr4)` — checks PATH for the antlr4 command |
| 2 | If found: sets `ANTLR4_TOOL_COMMAND` to the executable path |
| 3 | If not found: requires Java, downloads `antlr-4.13.2-complete.jar` into `${CMAKE_BINARY_DIR}`, sets `ANTLR4_TOOL_COMMAND` to `java -jar <jar>` |

The JAR is downloaded once and cached — subsequent configures skip the download.

`CMakeLists.txt` now includes this module in the Phase 4 dependencies section:

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(ANTLR4Tool)
```

After configuration, `${ANTLR4_TOOL_COMMAND}` is a CMake list ready for use in
`add_custom_command(COMMAND ${ANTLR4_TOOL_COMMAND} -Dlanguage=Cpp ...)`.

### Remaining 4.1 work

- Fetch `Sv.g4` into `grammar/Sv.g4`
- Add ANTLR4 C++ runtime (FetchContent from `antlr/antlr4 runtime/Cpp`)
- Wire `add_custom_command` to generate lexer/parser from the grammar at build time
- Unit test: parse a minimal SystemVerilog snippet and verify the parse tree root

### Testing the tool detection

```bash
# Normal configure — should print "Found antlr4 tool: ..."
cmake --preset debug

# Simulate missing antlr4 (forces JAR download path)
cmake -S . -B /tmp/test-fallback -DCMAKE_CXX_COMPILER=g++-13 \
  -DANTLR4_EXECUTABLE=ANTLR4_EXECUTABLE-NOTFOUND
# Should print: antlr4 not found in PATH — will use JAR fallback
# then: Downloading ANTLR4 4.13.2 JAR... / Using cached ANTLR4 JAR: ...
```

---

## 4.2 SystemVerilog Example Library

**Status:** Pending

20 `.sv` fixture files covering major SystemVerilog language features, used as parser
test inputs. `module_basic.sv` and `module_params.sv` already exist.

---

## 4.3 AST Visitor / Listener

**Status:** Pending

ANTLR4 listener that walks the parse tree and emits structured records.
Unit test each callback against the corresponding example file.

---

## 4.4 Symbol Extraction

**Status:** Pending

Extract: module/interface/package names, port declarations, signal declarations,
function/task signatures, class hierarchies, macro definitions.

---

## 4.5 Error Recovery

**Status:** Pending

ANTLR4 error listener that converts parse errors to `lsp::Diagnostic` objects.
Unit test with intentionally malformed SystemVerilog snippets.

---

## 4.6 Incremental Parsing

**Status:** Pending (depends on Phase 5 SQLite layer for file hashing)

Re-parse only files that have changed since the last compilation.

---

## Key decisions

| Decision | Choice | Rationale |
|---|---|---|
| Grammar source | `miguel-guerrero/antlr4_system_verilog_parser` `Sv.g4` | Covers SV-2012; widely referenced |
| ANTLR4 version | 4.13.2 | Matches installed `antlr4-tools` on dev machine |
| Tool detection | PATH first, JAR fallback | Zero-friction for devs who have antlr4-tools; still works on clean CI |
| C++ runtime | FetchContent from antlr/antlr4 runtime/Cpp | No vendoring; pinned to matching version |
