#pragma once
#include <string>
#include <vector>

struct PreprocessorResult {
    std::string source;
    std::vector<std::string> errors;
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
// Stringification (`") and token-pasting (``) are not supported in this
// implementation; use a slang-backed implementation for UVM-heavy codebases.
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
