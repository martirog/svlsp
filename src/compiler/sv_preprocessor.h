#pragma once
#include "compiler/parse_record.h"
#include <iosfwd>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>

struct MacroRecord {
    std::string name;
    std::string body;  // macro body text (trailing comment already stripped)
    int         line;  // 1-based line number in `file`
    int         column{0};          // 0-based column of the name on `line`
    std::string file{};             // defining file; empty = the compiled file
    bool        isFunctionLike{false};
    std::vector<std::string> params{};
    // Parallel to `params`; nullopt = no default (`define M(A, B=1)).
    std::vector<std::optional<std::string>> defaults{};
    std::string doc{};  // cleaned doc comment above/after the `define (plan.md §6.31)
};

// Where an `include pulled a file in: the directive's file (empty = the
// compiled file) and 1-based line. One per `include resolved, in order.
// One per file the preprocessor read, at its first `include.
struct IncludeSite {
    std::string file;      // the included file, as ParseRecord::file names it
    std::string includer;  // the file holding the `include; empty = the compiled file
    int         line{0};
    // The identity of the file as read (plan.md §6.34 S1): the content
    // hash of the text read, and size / mtime from a stat taken before
    // reading, at `statNs`. A change after the stat moves the mtime, so a
    // later check that finds the same stat data saw the same content
    // unless the change came within the mtime's granularity of `statNs`.
    std::string hash{};
    int64_t     size{-1};
    int64_t     mtimeNs{0};
    int64_t     statNs{0};
};

// One `include directive (plan.md §6.34 S1): the name as written, the file
// it is written in (resolution starts beside it; the compiled file's own
// path for its own directives), and the file it named -- "" when none was
// found. A compile's result depends on each resolving the same way again,
// which is what lets it ignore the include-dir list itself.
struct IncludeDirective {
    std::string name;
    std::string fromFile;
    std::string resolved;
};

// The file `include "name" names, written in `currentFile`: an absolute
// name as is, else the first `currentFile`'s directory, then `includeDirs`
// in order, holds -- normalized. Never the working directory. "" if none.
std::string resolveInclude(const std::string& name, const std::string& currentFile,
                           const std::vector<std::string>& includeDirs);

// One preprocessor problem, with where it is (plan.md §6.35 P1). `file` is
// the file the problem is written in -- "" for the compiled file, as
// ParseRecord::file -- `line` is 1-based, `column` / `endColumn` are
// 0-based and end-exclusive. An `include error covers its directive, from
// the backtick to the end of the file name (`endColumn` is the closing
// quote's column + 1); a conditional directive's error covers the
// directive; any other error covers the line it was found on, from its
// first non-blank character to the end of its code (before a `//`).
struct PreprocessorError {
    // An `include the preprocessor couldn't follow. A name not found is an
    // IncludeDirective with no resolution; any other include failure means
    // the result depends on something not recorded, so it is never reused.
    enum class Kind {
        IncludeNotFound,
        IncludeCycle,       // a file entered a third time (guards allow two)
        IncludeDepth,       // nested deeper than the limit
        IncludeUnreadable,  // resolved, but could not be opened
        IncludeMalformed,   // no "name" / <name> after `include
        MacroUndefined,     // `NAME with no `define of NAME (plan.md §6.35 P4)
        Other,              // any other preprocessor error
    };
    std::string message;
    std::string file{};
    int         line{0};
    int         column{0};
    int         endColumn{0};
    Kind        kind{Kind::Other};

    bool isInclude() const { return kind <= Kind::IncludeMalformed; }
};

struct PreprocessorResult {
    std::string source;
    std::vector<PreprocessorError> errors;
    std::vector<MacroRecord> macros;     // all `define macros encountered in active branches
    std::vector<SourceLine>  sourceMap;  // one entry per output line; index = line - 1
    std::vector<IncludeSite> includeSites;
    std::vector<IncludeDirective> includeDirectives; // distinct (fromFile, name), in order
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

    // Add a predefined macro with its full definition (function-like ones
    // too): the macros the file's includer had defined at its `include, for
    // a file compiled on its own. Not reported in PreprocessorResult::macros.
    void define(const MacroRecord& macro);

    // Preprocess `source` from the file at `filepath`.
    // Each call starts with a fresh macro table seeded from predefined macros.
    //
    // `progressLog`, if non-null, gets one "[parsed]   included: <path>" line
    // written to it the moment each `` `include `` is actually resolved and
    // opened -- in real time, as the recursive-descent include walk reaches
    // it, not after this whole call returns. This is what lets a caller like
    // CompilationController's own `--build-db` progress counter show
    // incremental feedback even for a single top-level file that `` `include ``s
    // an entire library (e.g. UVM's usual one-`uvm_pkg.sv`-includes-everything
    // shape): the alternative -- logging once per file after the whole
    // preprocess+parse of the top-level file finishes -- gives no signal at
    // all until the very end, since `` `include `` resolution and the ANTLR
    // parse both happen inside this one call.
    PreprocessorResult process(const std::string& source, const std::string& filepath,
                                std::ostream* progressLog = nullptr);

private:
    std::vector<std::string> m_includePaths;
    std::vector<std::pair<std::string, std::string>> m_predefined;
    std::vector<MacroRecord> m_seeded;
};
