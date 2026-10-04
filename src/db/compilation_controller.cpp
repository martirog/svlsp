#include "db/compilation_controller.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/file_utils.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <algorithm>
#include <functional>
#include <optional>
#include <ostream>
#include <unordered_map>

namespace {

// True if the declared parameter at `portIndex` received a real value at
// this call site: by position (an actual expression, not an elided slot --
// plan.md §6.23 allows skipping a defaulted parameter positionally while
// still supplying later ones by position or name), or by a named
// `.portName(...)` connection anywhere in the call (named slots always
// trail any positional ones, but are matched by name, not position).
bool paramSupplied(size_t portIndex, const std::vector<CallArgSlot>& args,
                    const std::string& portName)
{
    if (portIndex < args.size() && args[portIndex].kind == CallArgSlot::Kind::Positional)
        return true;
    for (const auto& a : args)
        if (a.kind == CallArgSlot::Kind::Named && a.name == portName) return true;
    return false;
}

// Flags each declared parameter of `calls[i]`'s callee (`callees[i]`, from
// the controller's CalleeResolver) that received no value at the call site
// and has no default -- plan.md §6.23. A call whose callee didn't resolve
// is skipped, not flagged: that is either a typo/unresolved-reference
// concern (plan.md §6.21) or a qualifier the resolver fails closed on
// (§6.26), not this check's job either way.
std::vector<ParseError> checkMissingArguments(
    SymbolDatabase& sdb, const std::vector<CallRecord>& calls,
    const std::vector<std::optional<SymbolRow>>& callees)
{
    std::vector<ParseError> diags;
    for (size_t c = 0; c < calls.size() && c < callees.size(); ++c) {
        const auto& call     = calls[c];
        const auto& resolved = callees[c];
        if (!resolved) continue;

        const std::string scope =
            resolved->scope.empty() ? resolved->name : resolved->scope + "::" + resolved->name;

        const std::vector<SymbolRow> ports = sdb.portsOf(scope);

        for (size_t i = 0; i < ports.size(); ++i) {
            if (paramSupplied(i, call.args, ports[i].name)) continue;
            if (ports[i].detail.find(PARAM_DEFAULT_VALUE_SEP) != std::string::npos) continue;
            diags.push_back({call.line, call.column,
                "missing required argument '" + ports[i].name + "' in call to '" +
                call.calleeName + "'"});
        }
    }
    return diags;
}

} // namespace

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
                                                        const ProjectConfig* config,
                                                        std::vector<std::string>* includedFiles,
                                                        bool forceRecompile)
{
    if (includedFiles) includedFiles->clear();

    const std::string hash = hashContent(text);

    // Cache hit: return diagnostics from DB without re-parsing. Included
    // files aren't reported here -- see this function's own doc comment.
    // `forceRecompile` skips this shortcut unconditionally (plan.md §6.4) --
    // needed when `path`'s own text is unchanged but something it
    // `` `include ``s just did.
    if (!forceRecompile && m_sdb.getFileHash(path) == hash) {
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
    auto walked = SvTreeWalker::walk(preprocessed.source, preprocessed.sourceMap, m_collectDocs);
    if (!m_collectDocs)
        for (auto& mac : preprocessed.macros) mac.doc.clear();

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
    std::unordered_map<std::string, std::vector<MacroRecord>> macrosByFile;
    for (const auto& mac : preprocessed.macros) macrosByFile[mac.file].push_back(mac);

    // Persist primary file.
    int64_t fid = m_sdb.upsertFile(path, hash);
    m_sdb.replaceSymbols(fid,        recsByFile[""]  );
    m_sdb.replaceDiagnostics(fid,    errsByFile[""]  );
    m_sdb.replaceImports(fid,        importsByFile[""]);
    m_sdb.replaceInstantiations(fid, instsByFile[""]  );
    m_sdb.replaceMacros(fid,         macrosByFile[""]);

    // Persist records/errors/imports/instantiations attributed to included files.
    // (The "[parsed]   included: <path>" progress line for each of these is
    // no longer logged here -- `preprocessor.process` above now logs it
    // itself, in real time as each `` `include `` is actually resolved,
    // rather than only once this entire pipeline finishes; see
    // SvPreprocessor::process's own doc comment for why that matters.)
    std::vector<std::string> allIncluded;
    std::vector<int64_t> recompiledIncFids; // instantiations replaced below
    for (const auto& [filePath, recs] : recsByFile) {
        if (filePath.empty()) continue;
        int64_t incFid = m_sdb.upsertFile(filePath, "");
        recompiledIncFids.push_back(incFid);
        m_sdb.replaceSymbols(incFid,        recs);
        m_sdb.replaceDiagnostics(incFid,    errsByFile[filePath]  );
        m_sdb.replaceImports(incFid,        importsByFile[filePath]);
        m_sdb.replaceInstantiations(incFid, instsByFile[filePath]  );
        m_sdb.replaceMacros(incFid,         macrosByFile[filePath] );
        allIncluded.push_back(filePath);
    }

    // A file with parse errors but no records at all (e.g. one that fails
    // to parse into anything recognizable) still needs its diagnostics
    // reported even though the loop above -- keyed off recsByFile -- never
    // saw it.
    for (const auto& [filePath, errs] : errsByFile) {
        if (filePath.empty() || recsByFile.count(filePath)) continue;
        int64_t incFid = m_sdb.upsertFile(filePath, "");
        m_sdb.replaceDiagnostics(incFid, errs);
        m_sdb.replaceMacros(incFid, macrosByFile[filePath]);
        allIncluded.push_back(filePath);
    }

    // A header holding only `define`s (UVM's uvm_macros.svh and friends)
    // has neither records nor errors: persist its macros, and list it as
    // included so editing it recompiles its includers (plan.md §6.29 A).
    for (const auto& [filePath, macs] : macrosByFile) {
        if (filePath.empty() || recsByFile.count(filePath) || errsByFile.count(filePath)) continue;
        int64_t incFid = m_sdb.upsertFile(filePath, "");
        m_sdb.replaceMacros(incFid, macs);
        allIncluded.push_back(filePath);
    }

    // replaceDiagnostics above kept LibraryResolver's 'library' rows
    // (unresolved instantiations); re-anchor them to the instantiations just recorded -- after every
    // file of this unit is persisted, so a module it now declares clears
    // them. The primary's are part of the return value, as below.
    std::vector<ParseError> primaryUnresolvedDiags = m_sdb.refreshLibraryDiagnostics(fid);
    for (int64_t incFid : recompiledIncFids) m_sdb.refreshLibraryDiagnostics(incFid);

    // Missing-required-argument check (plan.md §6.23) -- run only once every
    // symbol from this whole compile unit (primary + every included file)
    // is already persisted above, so a callee declared later in the same
    // file, or in one of its own `` `include ``s, resolves correctly
    // regardless of AST-walk visitation order. `appendDiagnostics` (not
    // another replaceDiagnostics) since each file's own parse-error
    // diagnostics were already freshly replaced above in this same call --
    // appending on top is never stale, since this whole pipeline reruns
    // from scratch on every real recompile (a cache hit never reaches this
    // code at all, see the early return above).
    std::unordered_map<std::string, std::vector<CallRecord>> callsByFile;
    for (const auto& call : walked.calls) callsByFile[call.file].push_back(call);

    // The primary file's own call diagnostics must also be merged into this
    // function's *return* value, not just persisted -- callers publish the
    // primary URI's diagnostics straight from that return value
    // (LanguageServer::parseDiagnostics), never by re-querying the DB the
    // way included files' diagnostics are (LanguageServer::
    // collectIncludedDiagnostics, which runs after this call returns and so
    // sees appendDiagnostics' effect regardless).
    // The callee is resolved against each file's own source text: `text`
    // for the primary, the file on disk for an included one (what the
    // preprocessor read it from).
    std::vector<ParseError> primaryCallDiags;
    for (const auto& [filePath, calls] : callsByFile) {
        if (calls.empty() || !m_calleeResolver) continue;
        std::optional<std::string> incText;
        if (!filePath.empty() && !(incText = readFile(filePath))) continue;
        const auto callees = m_calleeResolver(m_sdb, filePath.empty() ? path : filePath,
                                              filePath.empty() ? text : *incText, calls);
        auto diags = checkMissingArguments(m_sdb, calls, callees);
        if (diags.empty()) continue;
        if (filePath.empty()) {
            m_sdb.appendDiagnostics(fid, diags);
            primaryCallDiags = std::move(diags);
        } else {
            int64_t incFid = m_sdb.upsertFile(filePath, "");
            m_sdb.appendDiagnostics(incFid, diags);
        }
    }

    // Always persisted (plan.md §6.4), independent of whether a caller asked
    // for `includedFiles` -- this is what lets a *different* file's own
    // didSave later find "who includes me" via SymbolDatabase::includersOf.
    m_sdb.replaceFileIncludes(fid, allIncluded);
    if (includedFiles) *includedFiles = std::move(allIncluded);

    auto result = errsByFile[""];
    result.insert(result.end(), primaryCallDiags.begin(), primaryCallDiags.end());
    result.insert(result.end(), primaryUnresolvedDiags.begin(), primaryUnresolvedDiags.end());
    return result;
}
