#pragma once
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <string>
#include <vector>

// Orchestrates the incremental compilation pipeline.
//
// On each compile(path, text) call:
//   • Compute a content hash of `text`.
//   • If the stored hash in the DB matches → return cached diagnostics (no
//     re-parse).
//   • Otherwise → run CompilerDirectiveStripper → SvPreprocessor →
//     SvTreeWalker, persist symbols + diagnostics, update the file hash.
//
// This replaces the in-memory ParseCache from Phase 4.6 with a DB-backed
// equivalent that will survive server restarts once the Database is opened
// against a file path instead of ":memory:".
class CompilationController {
public:
    explicit CompilationController(SymbolDatabase& sdb);

    // Returns the ParseErrors for `path` / `text`, using the DB cache when
    // the content hash matches.
    std::vector<ParseError> compile(const std::string& path,
                                    const std::string& text);

private:
    SymbolDatabase& m_sdb;

    static std::string hashContent(const std::string& text);
};
