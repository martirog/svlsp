#pragma once
#include "compiler/parse_record.h"
#include <string>
#include <vector>

struct WalkResult {
    std::vector<ParseRecord> records;
    int parseErrors{0};
};

// Runs the full ANTLR4 parse pipeline on already-preprocessed SV source and
// returns structured records for every named declaration encountered:
// modules, interfaces, packages, classes, functions, tasks, and ANSI ports.
//
// The source must have been through CompilerDirectiveStripper (pass 1) and
// SvPreprocessor (pass 2) before being passed here.
class SvTreeWalker {
public:
    static WalkResult walk(const std::string& source);
};
