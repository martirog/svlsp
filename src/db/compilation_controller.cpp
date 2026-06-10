#include "db/compilation_controller.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <functional>

CompilationController::CompilationController(SymbolDatabase& sdb)
    : m_sdb{sdb}
{}

std::string CompilationController::hashContent(const std::string& text)
{
    // std::hash gives a fast content fingerprint suitable for within-session
    // change detection.  Phase 5.4 upgrade path: replace with SHA-256 for
    // persistence across restarts.
    return std::to_string(std::hash<std::string>{}(text));
}

std::vector<ParseError> CompilationController::compile(const std::string& path,
                                                        const std::string& text)
{
    const std::string hash = hashContent(text);

    // Cache hit: return diagnostics from DB without re-parsing.
    if (m_sdb.getFileHash(path) == hash) {
        auto rows = m_sdb.diagnosticsForFile(path);
        std::vector<ParseError> errs;
        errs.reserve(rows.size());
        for (const auto& r : rows)
            errs.push_back({r.line, r.col, r.message});
        return errs;
    }

    // Cache miss: run the full pipeline.
    auto stripped     = CompilerDirectiveStripper::strip(text, path);
    SvPreprocessor preprocessor;
    auto preprocessed = preprocessor.process(stripped.source, path);
    auto walked       = SvTreeWalker::walk(preprocessed.source);

    // Persist results.
    int64_t fid = m_sdb.upsertFile(path, hash);
    m_sdb.replaceSymbols(fid, walked.records);
    m_sdb.replaceDiagnostics(fid, walked.parseErrors);

    return walked.parseErrors;
}
