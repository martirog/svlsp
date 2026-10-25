#pragma once
#include "compiler/parse_record.h"
#include <string>
#include <unordered_map>
#include <vector>

struct WalkResult {
    std::vector<ParseRecord>  records;
    std::vector<ParseError>   parseErrors;
    std::vector<ImportRecord> imports;
    std::vector<InstantiationRecord> instantiations;
    std::vector<CallRecord>   calls;
    // Each included file's include scope: the scope chain open at its
    // `include (e.g. "uvm_pkg"), keyed like ParseRecord::file. A file
    // with no tokens of its own (only `define's) has no entry.
    std::unordered_map<std::string, std::string> includeScopes;
};

// Runs the full ANTLR4 parse pipeline on already-preprocessed SV source and
// returns structured records for every named declaration encountered:
// modules, interfaces, packages, classes, functions, tasks, and ANSI ports.
//
// The source must have been through CompilerDirectiveStripper (pass 1) and
// SvPreprocessor (pass 2) before being passed here.
//
// If `sourceMap` is provided (one SourceLine per output line of the
// preprocessor), every ParseRecord line and ParseError line is translated back
// to its original file and line number before being returned.  When empty,
// ANTLR4 line numbers are used as-is.
//
// `collectDocs` false skips doc-comment extraction (ParseRecord::doc stays
// "", plan.md §6.31).
//
// `initialScope` is the scope chain the source sits in ("" = the
// compilation unit): an included file compiled on its own starts in the
// scope open at its `include (e.g. "uvm_pkg"), so its records are scoped
// as when its includer is compiled.
//
// `prediction` is how ANTLR predicts (plan.md §6.33 C2). TwoStage, what
// svlsp uses: SLL, and if that finds a syntax error, a full LL parse from
// the start -- the grammar is SLL-clean, so a file without errors never
// pays for LL, and a file with errors gets LL's error recovery and
// messages. LL and SLL alone are for tools/grammar_profile and tests.
enum class ParsePrediction { TwoStage, LL, SLL };

class SvTreeWalker {
public:
    static WalkResult walk(const std::string& source,
                           const std::vector<SourceLine>& sourceMap = {},
                           bool collectDocs = true,
                           const std::string& initialScope = "",
                           ParsePrediction prediction = ParsePrediction::TwoStage);
};
