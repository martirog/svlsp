#pragma once
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include "compiler/project_config.h"
#include <iosfwd>
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
    // `logStream`, when non-null, receives one line per file this controller
    // parses/persists (the primary file on every call, plus every `include`d
    // file discovered on a cache-miss pass) — lets a caller confirm a
    // project's full expected file set actually got parsed rather than
    // silently missing files (e.g. a misconfigured include path).
    explicit CompilationController(SymbolDatabase& sdb, std::ostream* logStream = nullptr);

    // Returns the ParseErrors for `path` / `text`, using the DB cache when
    // the content hash matches. When `config` is non-null, its includeDirs
    // and defines seed the preprocessor (project-aware compilation); when
    // null (the default), preprocessing behaves exactly as before Phase 6.2
    // — no defines, no extra include directories.
    //
    // When `includedFiles` is non-null, it is cleared and then populated
    // with every distinct, transitively `` `include ``d file this call
    // actually recompiled (symbols/diagnostics for each are already
    // persisted to the DB either way — see `replaceDiagnostics` below —
    // this only reports *which* paths so a caller can also publish their
    // diagnostics, e.g. LanguageServer::compileAndPublish). Left empty on a
    // cache hit: the primary file's own text didn't change, so nothing new
    // needs reporting for files it includes either (a real but out-of-scope
    // gap this deliberately doesn't fix: an included file changing on disk
    // independently of the primary file's own hash isn't detected here —
    // see plan.md §6.4, cross-file invalidation).
    std::vector<ParseError> compile(const std::string& path,
                                    const std::string& text,
                                    const ProjectConfig* config = nullptr,
                                    std::vector<std::string>* includedFiles = nullptr);

private:
    SymbolDatabase& m_sdb;
    std::ostream*   m_log;

    static std::string hashContent(const std::string& text);
};
