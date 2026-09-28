#pragma once
#include "compiler/parse_record.h"
#include <string>
#include <vector>

struct WalkResult {
    std::vector<ParseRecord>  records;
    std::vector<ParseError>   parseErrors;
    std::vector<ImportRecord> imports;
    std::vector<InstantiationRecord> instantiations;
    std::vector<CallRecord>   calls;
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
class SvTreeWalker {
public:
    static WalkResult walk(const std::string& source,
                           const std::vector<SourceLine>& sourceMap = {},
                           bool collectDocs = true);
};
