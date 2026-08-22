#pragma once
#include "compiler/parse_record.h"
#include <string>
#include <vector>

struct MacroRecord {
    std::string name;
    std::string body;  // macro body text (trailing comment already stripped)
    int         line;  // 1-based line number in the original source file
};

struct PreprocessorResult {
    std::string source;
    std::vector<std::string> errors;
    std::vector<MacroRecord> macros;     // all `define macros encountered in active branches
    std::vector<SourceLine>  sourceMap;  // one entry per output line; index = line - 1
};

// Pass 2 of the two-pass SV preprocessing pipeline.
//
// Resolves text-transforming IEEE 1800 directives against source that has
// already been through CompilerDirectiveStripper (pass 1):
//   `define / `undef / `undefineall  — macro definition and removal
//   `ifdef / `ifndef / `elsif / `else / `endif  — conditional compilation
//   `include  — file inclusion
//   `NAME  — macro invocation (object-like and function-like)
//
// Each directive line is replaced with a blank line to preserve line numbers
// for all directives except `include, which replaces the line with the
// included file's content.
//
// Stringification (`"..`") is supported: the text between the two markers is
// macro-expanded, then wrapped in a quoted string literal. Token-pasting
// (``) is supported as a textual splice: each `` `` `` is deleted and the
// surrounding text glued together directly (matching every real-world usage
// found in the UVM library: identifier-fragment concatenation, pasting a
// macro-body prefix/suffix onto a parameter, and pasting a new macro name
// that is then itself invoked). Not supported: pasting where an operand is
// an *unexpanded* nested macro invocation whose expanded result (not its
// literal name) is needed on one side of the splice -- C's `##` doesn't
// expand such operands either, and no real SV source relies on it.
class SvPreprocessor {
public:
    explicit SvPreprocessor(std::vector<std::string> includePaths = {});

    // Add a predefined macro (like a command-line -D flag).
    // Value may be empty for flag-style defines.
    void define(const std::string& name, const std::string& value = "");

    // Preprocess `source` from the file at `filepath`.
    // Each call starts with a fresh macro table seeded from predefined macros.
    PreprocessorResult process(const std::string& source, const std::string& filepath);

private:
    std::vector<std::string> m_includePaths;
    std::vector<std::pair<std::string, std::string>> m_predefined;
};
