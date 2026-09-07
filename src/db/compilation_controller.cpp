#include "db/compilation_controller.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <functional>
#include <ostream>
#include <unordered_map>

CompilationController::CompilationController(SymbolDatabase& sdb, std::ostream* logStream)
    : m_sdb{sdb}
    , m_log{logStream}
{}

std::string CompilationController::hashContent(const std::string& text)
{
    // std::hash gives a fast content fingerprint suitable for within-session
    // change detection.  Phase 5.4 upgrade path: replace with SHA-256 for
    // persistence across restarts.
    return std::to_string(std::hash<std::string>{}(text));
}

std::vector<ParseError> CompilationController::compile(const std::string& path,
                                                        const std::string& text,
                                                        const ProjectConfig* config)
{
    const std::string hash = hashContent(text);

    // Cache hit: return diagnostics from DB without re-parsing.
    if (m_sdb.getFileHash(path) == hash) {
        if (m_log) { *m_log << "[parsed] " << path << " (cached)\n"; m_log->flush(); }
        auto rows = m_sdb.diagnosticsForFile(path);
        std::vector<ParseError> errs;
        errs.reserve(rows.size());
        for (const auto& r : rows)
            errs.push_back({r.line, r.col, r.message});
        return errs;
    }

    if (m_log) { *m_log << "[parsed] " << path << "\n"; m_log->flush(); }

    // Cache miss: run the full pipeline.
    auto stripped = CompilerDirectiveStripper::strip(text, path);
    SvPreprocessor preprocessor = config ? SvPreprocessor(config->includeDirs) : SvPreprocessor();
    if (config) {
        for (const auto& [name, value] : config->defines) preprocessor.define(name, value);
    }
    auto preprocessed = preprocessor.process(stripped.source, path, m_log);
    auto walked       = SvTreeWalker::walk(preprocessed.source, preprocessed.sourceMap);

    // Partition records, errors, imports, and instantiations by their
    // original source file. An empty `file` field means the record belongs
    // to the primary compiled file.
    std::unordered_map<std::string, std::vector<ParseRecord>>  recsByFile;
    std::unordered_map<std::string, std::vector<ParseError>>   errsByFile;
    std::unordered_map<std::string, std::vector<ImportRecord>> importsByFile;
    std::unordered_map<std::string, std::vector<InstantiationRecord>> instsByFile;

    for (const auto& rec : walked.records)     recsByFile[rec.file].push_back(rec);
    for (const auto& err : walked.parseErrors) errsByFile[err.file].push_back(err);
    for (const auto& imp : walked.imports)     importsByFile[imp.file].push_back(imp);
    for (const auto& inst : walked.instantiations) instsByFile[inst.file].push_back(inst);

    // Persist primary file.
    int64_t fid = m_sdb.upsertFile(path, hash);
    m_sdb.replaceSymbols(fid,        recsByFile[""]  );
    m_sdb.replaceDiagnostics(fid,    errsByFile[""]  );
    m_sdb.replaceImports(fid,        importsByFile[""]);
    m_sdb.replaceInstantiations(fid, instsByFile[""]  );

    // Persist records/errors/imports/instantiations attributed to included files.
    // (The "[parsed]   included: <path>" progress line for each of these is
    // no longer logged here -- `preprocessor.process` above now logs it
    // itself, in real time as each `` `include `` is actually resolved,
    // rather than only once this entire pipeline finishes; see
    // SvPreprocessor::process's own doc comment for why that matters.)
    for (const auto& [filePath, recs] : recsByFile) {
        if (filePath.empty()) continue;
        int64_t incFid = m_sdb.upsertFile(filePath, "");
        m_sdb.replaceSymbols(incFid,        recs);
        m_sdb.replaceDiagnostics(incFid,    errsByFile[filePath]  );
        m_sdb.replaceImports(incFid,        importsByFile[filePath]);
        m_sdb.replaceInstantiations(incFid, instsByFile[filePath]  );
    }

    return errsByFile[""];
}
