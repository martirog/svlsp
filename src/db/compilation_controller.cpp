#include "db/compilation_controller.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/file_utils.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <algorithm>
#include <chrono>
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
            if (portHasDefault(ports[i].detail)) continue;
            diags.push_back({call.line, call.column,
                "missing required argument '" + ports[i].name + "' in call to '" +
                call.calleeName + "'"});
        }
    }
    return diags;
}

// An Information diagnostic on line 1 of a file `include'd in more than
// one scope (in one compile or several), naming the scope svlsp uses
// (SymbolDatabase::includeContextOf, the first site) and the others. {} for
// a file whose sites all agree.
std::vector<ParseError> contextDiagnostics(const SymbolDatabase& sdb, const std::string& path)
{
    const auto sites = sdb.includeSitesOf(path);
    std::vector<const IncludeSiteRow*> distinct;
    for (const auto& s : sites)
        if (std::none_of(distinct.begin(), distinct.end(),
                         [&](const IncludeSiteRow* d) { return d->scope == s.scope; }))
            distinct.push_back(&s);
    if (distinct.size() < 2) return {};

    auto name = [](const std::string& scope) {
        return scope.empty() ? std::string{"the compilation unit"} : "'" + scope + "'";
    };
    std::string msg = "svlsp: `include'd in " + std::to_string(distinct.size()) +
                      " different scopes:";
    for (size_t i = 0; i < distinct.size(); ++i)
        msg += (i ? ", " : " ") + name(distinct[i]->scope) + " (" + distinct[i]->includerPath +
               ":" + std::to_string(distinct[i]->line) + ")";
    msg += "; using " + name(distinct.front()->scope);
    return {{1, 0, msg, {}, 3}};
}

} // namespace

CompilationController::CompilationController(SymbolDatabase& sdb, std::ostream* logStream)
    : m_sdb{sdb}
    , m_log{logStream}
{}

WalkResult CompilationController::walk(const std::string& source,
                                       const std::vector<SourceLine>& sourceMap,
                                       bool collectDocs, const std::string& initialScope) const
{
    return m_walker ? m_walker(source, sourceMap, collectDocs, initialScope)
                    : SvTreeWalker::walk(source, sourceMap, collectDocs, initialScope);
}

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
                                                        bool forceRecompile,
                                                        std::vector<std::string>* refreshedFiles)
{
    if (includedFiles) includedFiles->clear();
    if (refreshedFiles) refreshedFiles->clear();
    auto job = prepare(path, text, config, forceRecompile);
    if (job.cached) return std::move(*job.cached);
    return commit(parse(std::move(job)), includedFiles, refreshedFiles);
}

CompilationController::Prepared CompilationController::prepare(const std::string& path,
                                                               const std::string& text,
                                                               const ProjectConfig* config,
                                                               bool forceRecompile)
{
    Prepared job{.path = path, .text = text, .hash = hashContent(text),
                 .preprocessor = config ? SvPreprocessor(config->includeDirs) : SvPreprocessor()};
    job.checkTypes = config != nullptr;

    // Cache hit: return diagnostics from DB without re-parsing. Included
    // files aren't reported here -- see compile()'s own doc comment.
    // `forceRecompile` skips this shortcut unconditionally (plan.md §6.4) --
    // needed when `path`'s own text is unchanged but something it
    // `` `include ``s just did.
    if (!forceRecompile && m_sdb.getFileHash(path) == job.hash) {
        if (m_log) { *m_log << "[parsed] " << path << " (cached)\n"; m_log->flush(); }
        auto rows = m_sdb.diagnosticsForFile(path);
        std::vector<ParseError> errs;
        errs.reserve(rows.size());
        for (const auto& r : rows)
            errs.push_back({r.line, r.col, r.message, {}, r.severity});
        job.cached = std::move(errs);
        return job;
    }

    if (m_log) { *m_log << "[parsed] " << path << "\n"; m_log->flush(); }

    // Cache miss: the full pipeline runs in parse().
    if (config) {
        for (const auto& [name, value] : config->defines) job.preprocessor.define(name, value);
    }

    // An included file compiled on its own (opened or edited) starts where
    // its includer left off: in the scope open at its `include, with the
    // macros its includer's unit defines -- so its records are scoped as
    // when the includer is compiled (`uvm_pkg::uvm_component`, not
    // `uvm_component`).
    const auto context = m_sdb.includeContextOf(path);
    if (context) {
        for (const auto& m : m_sdb.macrosOfUnit(*context, path))
            job.preprocessor.define(MacroRecord{m.name, m.body, m.line, m.col, m.filePath,
                                                m.isFunctionLike, m.params, m.defaults});
        if (m_log) {
            *m_log << "[context]  " << path << ": scope '" << context->scope << "' from "
                   << context->includerPath << ":" << context->line << "\n";
            m_log->flush();
        }
        job.initialScope = context->scope;
    }
    return job;
}

CompilationController::Parsed CompilationController::parse(Prepared&& job) const
{
    auto stripped = CompilerDirectiveStripper::strip(job.text, job.path);
    auto preprocessed = job.preprocessor.process(stripped.source, job.path, m_log);
    auto walked = walk(preprocessed.source, preprocessed.sourceMap, m_collectDocs,
                       job.initialScope);
    if (!m_collectDocs)
        for (auto& mac : preprocessed.macros) mac.doc.clear();
    return {std::move(job.path), std::move(job.text), std::move(job.hash),
            std::move(preprocessed), std::move(walked), job.checkTypes};
}

std::vector<ParseError> CompilationController::commit(Parsed&& parsed,
                                                      std::vector<std::string>* includedFiles,
                                                      std::vector<std::string>* refreshedFiles)
{
    if (refreshedFiles) refreshedFiles->clear();
    const std::string& path = parsed.path;
    const std::string& text = parsed.text;
    const std::string& hash = parsed.hash;
    const auto& preprocessed = parsed.preprocessed;
    const auto& walked = parsed.walked;

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

    // Where this compile included each file (schema v16), the first
    // `include of it. The walker gives a file with no tokens of its own
    // (only `include's or `define's) the scopes open around its lines; only
    // a file with no lines at all gets the compilation unit's.
    std::vector<IncludeSiteRecord> sites;
    for (const auto& s : preprocessed.includeSites) {
        if (std::any_of(sites.begin(), sites.end(),
                        [&](const IncludeSiteRecord& r) { return r.file == s.file; }))
            continue;
        const auto it = walked.includeScopes.find(s.file);
        sites.push_back({s.file, s.includer.empty() ? path : s.includer, s.line,
                         it == walked.includeScopes.end() ? "" : it->second});
    }
    std::vector<std::string> sitedFiles = m_sdb.replaceIncludeSites(fid, path, sites);
    for (const auto& s : sites) sitedFiles.push_back(s.file);

    // Tell a file `include'd in more than one scope which one svlsp uses
    // (re-checked for each file this compile includes, or no longer does).
    const auto primaryContextDiags = contextDiagnostics(m_sdb, path);
    m_sdb.replaceDiagnostics(fid, primaryContextDiags, "context");
    std::sort(sitedFiles.begin(), sitedFiles.end());
    sitedFiles.erase(std::unique(sitedFiles.begin(), sitedFiles.end()), sitedFiles.end());
    for (const auto& file : sitedFiles)
        m_sdb.replaceDiagnostics(m_sdb.upsertFile(file, ""), contextDiagnostics(m_sdb, file),
                                 "context");
    // Declared types (plan.md §6.21 step 3), once the include sites above
    // are persisted: an included file's package-level positions are in the
    // scope it is `include'd in only through them.
    const std::vector<ParseError> primaryTypeDiags =
        parsed.checkTypes ? checkTypeRefs(parsed, fid, allIncluded) : std::vector<ParseError>{};
    refreshTypeRefDiagnostics(fid, allIncluded, refreshedFiles);

    if (includedFiles) *includedFiles = std::move(allIncluded);

    auto result = errsByFile[""];
    result.insert(result.end(), primaryContextDiags.begin(), primaryContextDiags.end());
    result.insert(result.end(), primaryCallDiags.begin(), primaryCallDiags.end());
    result.insert(result.end(), primaryTypeDiags.begin(), primaryTypeDiags.end());
    result.insert(result.end(), primaryUnresolvedDiags.begin(), primaryUnresolvedDiags.end());
    return result;
}

// The declared-type check (plan.md §6.21 step 3): each type name `parsed`'s
// unit writes at a declaration, in the primary file (`fid`) and each of
// `included`, checked once the whole unit is persisted, as 'reference'
// Warnings with the written name as subject. Every file of the unit has its
// old rows replaced, even with no references left. Returns the primary's.
std::vector<ParseError> CompilationController::checkTypeRefs(
    const Parsed& parsed, int64_t fid, const std::vector<std::string>& included)
{
    if (!m_typeChecker) return {};
    const auto start = std::chrono::steady_clock::now();
    const auto& refs = parsed.walked.typeRefs;

    // The unit's files' texts: the primary's, and each included file with
    // references, as the preprocessor read it.
    std::unordered_map<std::string, std::string> texts{{parsed.path, parsed.text}};
    for (const auto& ref : refs)
        if (!ref.file.empty() && !texts.count(ref.file))
            if (auto text = readFile(ref.file)) texts.emplace(ref.file, std::move(*text));
    const auto problems = m_typeChecker(m_sdb, parsed.path, texts, refs);

    std::unordered_map<std::string, std::vector<ParseError>> warningsByFile;
    std::unordered_map<std::string, std::vector<std::string>> subjectsByFile;
    for (size_t i = 0; i < refs.size() && i < problems.size(); ++i) {
        if (!problems[i]) continue;
        ParseError diag{refs[i].line, refs[i].column, *problems[i]};
        diag.severity = 2;
        warningsByFile[refs[i].file].push_back(diag);
        subjectsByFile[refs[i].file].push_back(
            refs[i].qualifier.empty() ? refs[i].name : refs[i].qualifier + "::" + refs[i].name);
    }

    // Every file of the unit has its old rows replaced, even with none left.
    std::vector<std::string> files{parsed.path};
    files.insert(files.end(), included.begin(), included.end());
    m_sdb.clearDiagnostics("reference", files);
    size_t warnings = 0;
    for (const auto& [filePath, diags] : warningsByFile) {
        const int64_t fileId = filePath.empty() ? fid : m_sdb.upsertFile(filePath, "");
        const auto& subjects = subjectsByFile[filePath];
        for (size_t i = 0; i < diags.size(); ++i)
            m_sdb.appendDiagnostics(fileId, {diags[i]}, "reference", subjects[i]);
        warnings += diags.size();
    }
    if (m_log) {
        const auto ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
        *m_log << "[types]    " << parsed.path << ": " << refs.size() << " refs, " << warnings
               << " warnings, " << static_cast<int>(ms) << " ms\n";
        m_log->flush();
    }
    return warningsByFile[""];
}

// Re-checks the 'reference' rows of files outside the unit just committed
// (`fid` and `included`), which may now resolve: a project load compiles a
// file before the one declaring its types, or the missing type was just
// added. Deletes the ones that do; their files go to `refreshedFiles`.
void CompilationController::refreshTypeRefDiagnostics(int64_t fid,
                                                      const std::vector<std::string>& included,
                                                      std::vector<std::string>* refreshedFiles)
{
    if (!m_typeChecker) return;
    std::vector<SubjectDiagnosticRow> rows;
    for (auto& row : m_sdb.diagnosticsFromSource("reference"))
        if (row.fileId != fid &&
            std::find(included.begin(), included.end(), row.filePath) == included.end())
            rows.push_back(std::move(row));
    for (size_t begin = 0; begin < rows.size();) {
        size_t end = begin;
        std::vector<TypeRefRecord> refs;
        for (; end < rows.size() && rows[end].fileId == rows[begin].fileId; ++end) {
            const auto& row = rows[end];
            const size_t sep = row.subject.rfind("::");
            refs.push_back({sep == std::string::npos ? row.subject : row.subject.substr(sep + 2),
                            sep == std::string::npos ? "" : row.subject.substr(0, sep), row.line,
                            row.col, row.filePath});
        }
        const auto problems = m_typeChecker(m_sdb, rows[begin].filePath, {}, refs);
        bool cleared = false;
        for (size_t i = 0; i < refs.size() && i < problems.size(); ++i)
            if (!problems[i]) {
                const auto& row = rows[begin + i];
                m_sdb.deleteDiagnostic(row.fileId, row.line, row.col, "reference");
                cleared = true;
            }
        if (cleared && refreshedFiles) refreshedFiles->push_back(rows[begin].filePath);
        begin = end;
    }
}
