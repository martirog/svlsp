#pragma once
#include <string>
#include <vector>

enum class DirectiveKind {
    Timescale,
    DefaultNettype,
    Celldefine,
    Endcelldefine,
    UnconnectedDrive,
    NounconnectedDrive,
    Resetall,
    BeginKeywords,
    EndKeywords,
    Pragma,
    Line,
};

struct DirectiveRecord {
    DirectiveKind kind;
    std::string   value; // argument text; empty for valueless directives
    int           line;  // 1-based line number in original source
};

struct StripResult {
    std::string                  source;
    std::vector<DirectiveRecord> directives;
};

// Pass 1 of the two-pass SV preprocessing pipeline.
//
// Removes IEEE 1800 compiler directives that carry only metadata (timescale,
// default_nettype, celldefine, etc.) and substitutes `__FILE__ / `__LINE__
// with literals derived from the original source file, before any `include
// insertion or temp-buffer creation can corrupt those values.
//
// Each removed directive line is replaced with a blank line so that
// subsequent passes see the same line numbers as the original source.
//
// Pass-2 preprocessor directives (`define, `ifdef, `include, macro
// invocations) are left untouched.
class CompilerDirectiveStripper {
public:
    // Strip compiler directives from `source`.
    // `filepath` is the original source path — used for `__FILE__ substitution.
    static StripResult strip(const std::string& source, const std::string& filepath);
};
