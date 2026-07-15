#pragma once
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include "compiler/project_config.h"
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
    // the content hash matches. When `config` is non-null, its includeDirs
    // and defines seed the preprocessor (project-aware compilation); when
    // null (the default), preprocessing behaves exactly as before Phase 6.2
    // — no defines, no extra include directories.
    std::vector<ParseError> compile(const std::string& path,
                                    const std::string& text,
                                    const ProjectConfig* config = nullptr);

private:
    SymbolDatabase& m_sdb;

    static std::string hashContent(const std::string& text);
};
