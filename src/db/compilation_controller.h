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
    // needs reporting for files it includes either.
    //
    // On every real recompile (never a cache hit), this same transitive
    // include set is also persisted via SymbolDatabase::replaceFileIncludes,
    // independent of whether `includedFiles` is non-null — this is what lets
    // a caller later ask "who includes path X" (SymbolDatabase::includersOf)
    // to find every file that needs recompiling when X itself changes
    // (plan.md §6.4, cross-file invalidation).
    //
    // `forceRecompile`, when true, skips the content-hash cache-hit check
    // unconditionally and always runs the full pipeline. Needed for exactly
    // this cross-file-invalidation case: a dependent file's own text may be
    // completely unchanged even though something it `` `include ``s just
    // changed, so the ordinary hash comparison alone would wrongly treat it
    // as a cache hit and skip re-parsing it — the plain compile() path where
    // the caller's own text really did change never needs this.
    std::vector<ParseError> compile(const std::string& path,
                                    const std::string& text,
                                    const ProjectConfig* config = nullptr,
                                    std::vector<std::string>* includedFiles = nullptr,
                                    bool forceRecompile = false);

private:
    SymbolDatabase& m_sdb;
    std::ostream*   m_log;

    static std::string hashContent(const std::string& text);
};
