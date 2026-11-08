#include "db/compilation_controller.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <ostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

// --- File identity (plan.md §6.34 S1) ------------------------------------

// `path`'s current content hash: the stored one when its stat data still
// matches and wasn't taken racily close to its mtime, else the hash of its
// text read now. nullopt if it can't be read (gone).
std::optional<std::string> currentHash(const SymbolDatabase& sdb, const std::string& path)
{
    const auto stored = sdb.storedFile(path);
    const auto st = statFile(path);
    if (!st) return std::nullopt;
    if (stored && stored->size >= 0 && stored->size == st->size &&
        stored->mtimeNs == st->mtimeNs && stored->mtimeNs + kRacyWindowNs < stored->statNs)
        return stored->hash;
    auto text = readFile(path);
    if (!text) return std::nullopt;
    return contentHash(*text);
}

// Appends a length-prefixed field, so no two field lists hash alike.
void addField(std::string& key, const std::string& field)
{
    key += std::to_string(field.size());
    key += ':';
    key += field;
}

// Everything a compile's result depends on besides its files' contents
// and its includes' resolution: the config's parse-affecting settings and
// the checks that run. A change of any of them makes every unit compiled
// under it miss the cache. The include dirs are not part of it: they
// matter only through what each `include resolves to, which is checked
// directive by directive (inputsUnchanged), so a branch that adds an
// include dir doesn't recompile the units that don't include from it.
std::string configKey(const ProjectConfig* config, bool collectDocs, bool calleeCheck,
                      bool typeCheck)
{
    std::string key = "svlsp-inputs-v1";
    addField(key, config ? "config" : "no-config");
    addField(key, collectDocs ? "docs" : "no-docs");
    addField(key, calleeCheck ? "callees" : "no-callees");
    addField(key, typeCheck && config ? "types" : "no-types");
    if (!config) return key;
    for (const auto& [name, value] : config->defines) {  // a std::map: sorted
        addField(key, name);
        addField(key, value);
    }
    addField(key, "|-y");
    for (const auto& d : config->libraryDirs) addField(key, d);
    addField(key, "|-v");
    for (const auto& f : config->libraryFiles) addField(key, f);
    addField(key, "|libext");
    for (const auto& e : config->libExtensions) addField(key, e);
    addField(key, config->mode == SvLanguageMode::Verilog95 ? "v95" : "sv");
    return key;
}

// The include context a file compiled on its own starts from: the scope at
// its `include and the includer's macros (prepare()).
void addContext(std::string& key, const std::optional<IncludeSiteRow>& context,
                const std::vector<MacroRow>& macros)
{
    addField(key, "|context");
    if (!context) return;
    addField(key, context->scope);
    addField(key, context->includerPath);
    addField(key, std::to_string(context->line));
    for (const auto& m : macros) {
        addField(key, m.name);
        addField(key, m.isFunctionLike ? "f" : "o");
        for (size_t i = 0; i < m.params.size(); ++i) {
            addField(key, m.params[i]);
            addField(key, i < m.defaults.size() && m.defaults[i] ? "=" + *m.defaults[i] : "");
        }
        addField(key, m.body);
        addField(key, m.filePath);
    }
}

// The fingerprint of a compile: its inputs key, the primary text's hash,
// and each file it read with its hash, by path.
std::string unitFingerprint(const std::string& inputsKey, const std::string& primaryHash,
                            std::vector<std::pair<std::string, std::string>> files)
{
    std::sort(files.begin(), files.end());
    std::string key = inputsKey;
    addField(key, primaryHash);
    for (const auto& [path, hash] : files) {
        addField(key, path);
        addField(key, hash);
    }
    return contentHash(key);
}

// Whether the stored fingerprint of `path` still describes its inputs, with
// its primary text hashing to `primaryHash` under `inputsKey`: every
// `include its last compile followed resolves to the same file with
// `includeDirs` (a missing one is still missing), and every file it read
// is unchanged.
bool inputsUnchanged(const SymbolDatabase& sdb, const std::string& path,
                     const std::string& primaryHash, const std::string& inputsKey,
                     const std::vector<std::string>& includeDirs)
{
    const auto stored = sdb.storedFile(path);
    if (!stored || stored->inputsFp.empty()) return false;
    for (const auto& d : sdb.includeDirectivesOf(path))
        if (resolveInclude(d.name, d.fromFile, includeDirs) != d.resolved) return false;
    std::vector<std::pair<std::string, std::string>> files;
    for (const auto& inc : sdb.includedFilesOf(path)) {
        auto hash = currentHash(sdb, inc);
        if (!hash) return false;
        files.emplace_back(inc, std::move(*hash));
    }
    return unitFingerprint(inputsKey, primaryHash, std::move(files)) == stored->inputsFp;
}

// Whether a compile's preprocessing failed to follow an `include for a
// reason other than a name not found (an IncludeDirective records that):
// a cycle, the depth limit, an unreadable file, a malformed directive. Its
// result depends on something not recorded, so it is never reused.
bool includeFailed(const PreprocessorResult& preprocessed)
{
    return std::any_of(preprocessed.errors.begin(), preprocessed.errors.end(),
                       [](const PreprocessorError& e) {
                           return e.isInclude() &&
                                  e.kind != PreprocessorError::Kind::IncludeNotFound;
                       });
}

// `path`'s file id, adding a row only for a file not known yet: a recheck
// must not reset the identity of the files it writes diagnostics on.
int64_t knownFileId(SymbolDatabase& sdb, const std::string& path)
{
    const int64_t fid = sdb.fileIdFor(path);
    return fid >= 0 ? fid : sdb.upsertFile(path, "");
}

// --- Stored check inputs (plan.md §6.34 D3) --------------------------------
// One record per line, fields separated by U+001F; a call's argument slots
// by U+001E ("P", "E", or "N" + the name). Names never hold these.

std::vector<std::string> splitOn(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t begin = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == sep) {
            out.push_back(s.substr(begin, i - begin));
            begin = i + 1;
        }
    return out;
}

int toInt(const std::string& s) { return s.empty() ? 0 : std::stoi(s); }

std::string serializeTypeRefs(const std::vector<TypeRefRecord>& refs)
{
    std::string out;
    for (const auto& r : refs)
        out += r.name + '\x1f' + r.qualifier + '\x1f' + std::to_string(r.line) + '\x1f' +
               std::to_string(r.column) + '\x1f' + r.file + '\n';
    return out;
}

std::vector<TypeRefRecord> parseTypeRefs(const std::string& data)
{
    std::vector<TypeRefRecord> refs;
    for (const auto& line : splitOn(data, '\n')) {
        const auto f = splitOn(line, '\x1f');
        if (f.size() < 5) continue;
        refs.push_back({f[0], f[1], toInt(f[2]), toInt(f[3]), f[4]});
    }
    return refs;
}

// `resolved[i]`: whether calls[i]'s callee resolved when it was last
// checked (a 10th field, "1" or "0").
std::string serializeCalls(const std::vector<CallRecord>& calls, const std::vector<bool>& resolved)
{
    std::string out;
    for (size_t n = 0; n < calls.size(); ++n) {
        const auto& c = calls[n];
        std::string args;
        for (size_t i = 0; i < c.args.size(); ++i) {
            if (i) args += '\x1e';
            const auto& a = c.args[i];
            args += a.kind == CallArgSlot::Kind::Positional ? "P"
                  : a.kind == CallArgSlot::Kind::Elided     ? "E"
                                                           : "N" + a.name;
        }
        out += c.calleeName + '\x1f' + c.calleeScope + '\x1f' + std::to_string(c.line) + '\x1f' +
               std::to_string(c.column) + '\x1f' + c.file + '\x1f' + std::to_string(c.nameLine) +
               '\x1f' + std::to_string(c.nameColumn) + '\x1f' + std::to_string(c.args.size()) +
               '\x1f' + args + '\x1f' + (n < resolved.size() && resolved[n] ? "1" : "0") + '\n';
    }
    return out;
}

std::vector<CallRecord> parseCalls(const std::string& data, std::vector<bool>& resolved)
{
    std::vector<CallRecord> calls;
    resolved.clear();
    for (const auto& line : splitOn(data, '\n')) {
        const auto f = splitOn(line, '\x1f');
        if (f.size() < 9) continue;
        CallRecord c;
        c.calleeName = f[0];
        c.calleeScope = f[1];
        c.line = toInt(f[2]);
        c.column = toInt(f[3]);
        c.file = f[4];
        c.nameLine = toInt(f[5]);
        c.nameColumn = toInt(f[6]);
        if (toInt(f[7]) > 0)
            for (const auto& a : splitOn(f[8], '\x1e')) {
                if (a == "P") c.args.push_back({CallArgSlot::Kind::Positional});
                else if (a == "E") c.args.push_back({CallArgSlot::Kind::Elided});
                else c.args.push_back({CallArgSlot::Kind::Named, a.substr(1)});
            }
        calls.push_back(std::move(c));
        resolved.push_back(f.size() > 9 && f[9] == "1");
    }
    return calls;
}

bool namesOneOf(const std::unordered_set<std::string>& names, const std::string& name)
{
    return names.count(name) > 0;
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
    return contentHash(text);
}

std::string CompilationController::configFingerprint(const ProjectConfig* config) const
{
    return contentHash(configKey(config, m_collectDocs, bool(m_calleeResolver), bool(m_typeChecker)));
}

std::string CompilationController::inputsKeyFor(const std::string& path,
                                                const ProjectConfig* config,
                                                std::optional<IncludeSiteRow>* context,
                                                std::vector<MacroRow>* contextMacros) const
{
    auto ctx = m_sdb.includeContextOf(path);
    std::vector<MacroRow> macros;
    if (ctx) macros = m_sdb.macrosOfUnit(*ctx, path);
    std::string key = configKey(config, m_collectDocs, bool(m_calleeResolver), bool(m_typeChecker));
    addContext(key, ctx, macros);
    if (context) *context = std::move(ctx);
    if (contextMacros) *contextMacros = std::move(macros);
    return key;
}

bool CompilationController::isUpToDate(const std::string& path, const ProjectConfig* config) const
{
    const auto hash = currentHash(m_sdb, path);
    if (!hash) return false;
    static const std::vector<std::string> kNoDirs;
    return inputsUnchanged(m_sdb, path, *hash, inputsKeyFor(path, config, nullptr, nullptr),
                           config ? config->includeDirs : kNoDirs);
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

    // An included file compiled on its own (opened or edited) starts where
    // its includer left off: in the scope open at its `include, with the
    // macros its includer's unit defines -- so its records are scoped as
    // when the includer is compiled (`uvm_pkg::uvm_component`, not
    // `uvm_component`).
    std::optional<IncludeSiteRow> context;
    std::vector<MacroRow> contextMacros;
    job.inputsKey = inputsKeyFor(path, config, &context, &contextMacros);

    // Cache hit: return diagnostics from DB without re-parsing, when every
    // input of the last compile is unchanged (plan.md §6.34 S1): this text,
    // each file it read, the config and the include context. Included
    // files aren't reported here -- see compile()'s own doc comment.
    // `forceRecompile` skips this shortcut unconditionally (plan.md §6.4).
    static const std::vector<std::string> kNoDirs;
    if (!forceRecompile && inputsUnchanged(m_sdb, path, job.hash, job.inputsKey,
                                           config ? config->includeDirs : kNoDirs)) {
        if (m_log) { *m_log << "[parsed] " << path << " (cached)\n"; m_log->flush(); }
        auto rows = m_sdb.diagnosticsForFile(path);
        std::vector<ParseError> errs;
        errs.reserve(rows.size());
        for (const auto& r : rows)
            errs.push_back({r.line, r.col, r.message, {}, r.severity, r.endCol});
        job.cached = std::move(errs);
        return job;
    }

    if (m_log) { *m_log << "[parsed] " << path << "\n"; m_log->flush(); }

    // Cache miss: the full pipeline runs in parse().
    if (config) {
        for (const auto& [name, value] : config->defines) job.preprocessor.define(name, value);
    }
    if (context) {
        for (const auto& m : contextMacros)
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

CompilationController::Parsed CompilationController::parseBuffered(Prepared&& job) const
{
    std::ostringstream log;
    Parsed parsed = parseTo(std::move(job), m_log ? &log : nullptr);
    parsed.bufferedLog = log.str();
    return parsed;
}

bool CompilationController::parseStillValid(const Parsed& parsed,
                                            const ProjectConfig* config) const
{
    return inputsKeyFor(parsed.path, config, nullptr, nullptr) == parsed.inputsKey;
}

CompilationController::Parsed CompilationController::parse(Prepared&& job) const
{
    return parseTo(std::move(job), m_log);
}

CompilationController::Parsed CompilationController::parseTo(Prepared&& job,
                                                             std::ostream* log) const
{
    // The primary file's stat data, when the text compiled is the file's
    // (not an unsaved buffer): stat first, as the preprocessor does for the
    // files it reads.
    const int64_t statNs = nowNs();
    SymbolDatabase::StoredFile primary{job.hash, -1, 0, 0, ""};
    if (const auto st = statFile(job.path);
        st && st->size == static_cast<int64_t>(job.text.size())) {
        if (const auto disk = readFile(job.path); disk && *disk == job.text)
            primary = {job.hash, st->size, st->mtimeNs, statNs, ""};
    }

    auto stripped = CompilerDirectiveStripper::strip(job.text, job.path);
    auto preprocessed = job.preprocessor.process(stripped.source, job.path, log);
    auto walked = walk(preprocessed.source, preprocessed.sourceMap, m_collectDocs,
                       job.initialScope);
    if (!m_collectDocs)
        for (auto& mac : preprocessed.macros) mac.doc.clear();
    return {std::move(job.path), std::move(job.text), std::move(job.hash),
            std::move(preprocessed), std::move(walked), job.checkTypes,
            std::move(job.inputsKey), std::move(primary)};
}

std::vector<ParseError> CompilationController::commit(Parsed&& parsed,
                                                      std::vector<std::string>* includedFiles,
                                                      std::vector<std::string>* refreshedFiles)
{
    if (refreshedFiles) refreshedFiles->clear();
    if (m_log && !parsed.bufferedLog.empty()) {
        *m_log << parsed.bufferedLog;
        m_log->flush();
    }
    // One transaction (plan.md §6.34 S5): a reader on another connection
    // never sees half a unit.
    Database::Transaction txn(m_sdb.database(), "unit_commit");
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

    // Preprocessor errors (plan.md §6.35), on the file each is in. Include
    // errors (P2) are 'include' rows over their directive: a malformed
    // directive a Warning, the rest Errors (I1). The others (P4) are
    // 'preprocess' rows, all Errors (an undefined macro too, decided
    // 2026-11-08). Only under a project config (I2: a file
    // on its own has no include dirs, so no library macros). Replaced on
    // every file this unit read, so one that no longer fails is cleared,
    // and a file whose rows came or went is reported for republishing even
    // when it has no rows of its own (a wrapper of `include's).
    using PPKind = PreprocessorError::Kind;
    std::unordered_map<std::string, std::vector<ParseError>> includeErrsByFile, ppErrsByFile;
    if (parsed.checkTypes)
        for (const auto& e : preprocessed.errors) {
            const bool warning = e.kind == PPKind::IncludeMalformed;
            (e.isInclude() ? includeErrsByFile : ppErrsByFile)[e.file].push_back(
                {e.line, e.column, e.message, e.file, warning ? 2 : 1, e.endColumn});
        }
    const std::pair<const char*, decltype(includeErrsByFile)*> ppSources[] = {
        {"include", &includeErrsByFile}, {"preprocess", &ppErrsByFile}};
    for (const auto& [source, byFile] : ppSources)
        m_sdb.replaceDiagnostics(fid, (*byFile)[""], source);
    for (const auto& s : preprocessed.includeSites) {
        if (s.file == path) continue;
        const auto before = m_sdb.diagnosticsForFile(s.file);
        bool changed = false;
        for (const auto& [source, byFile] : ppSources) {
            const std::string src = source;
            const bool had = std::any_of(before.begin(), before.end(),
                                         [&](const DiagnosticRow& d) { return d.source == src; });
            const auto it = byFile->find(s.file);
            const bool has = it != byFile->end();
            if (!had && !has) continue;
            m_sdb.replaceDiagnostics(m_sdb.upsertFile(s.file, ""),
                                     has ? it->second : std::vector<ParseError>{}, src);
            changed = true;
        }
        if (changed && std::find(allIncluded.begin(), allIncluded.end(), s.file) == allIncluded.end())
            allIncluded.push_back(s.file);
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
    // The callee is resolved against each file's own source text: `text`
    // for the primary, the file on disk for an included one (what the
    // preprocessor read it from). The primary's rows are also part of the
    // return value: callers publish the primary's diagnostics from it.
    std::vector<std::string> unitFiles{path};
    for (const auto& s : preprocessed.includeSites)
        if (s.file != path) unitFiles.push_back(s.file);
    for (const auto& f : allIncluded)
        if (std::find(unitFiles.begin(), unitFiles.end(), f) == unitFiles.end())
            unitFiles.push_back(f);
    std::vector<bool> resolvedCalls;
    const std::vector<ParseError> primaryCallDiags =
        runCallCheck(path, fid, text, walked.calls, unitFiles, resolvedCalls, nullptr);
    m_sdb.replaceUnitChecks(fid, "calls", serializeCalls(walked.calls, resolvedCalls));
    m_sdb.replaceUnitChecks(fid, "types",
                            parsed.checkTypes ? serializeTypeRefs(walked.typeRefs) : "");

    // Always persisted (plan.md §6.4), independent of whether a caller asked
    // for `includedFiles` -- this is what lets a *different* file's own
    // didSave later find "who includes me" via SymbolDatabase::includersOf.
    // Every file this compile read, also one with no rows of its own (a
    // wrapper of `include's): an edit to it changes the unit too (plan.md
    // §6.34 S1). A file the unit no longer reads, and no other unit does,
    // loses the rows this unit's last compile wrote for it.
    std::vector<std::string> readFiles;
    for (const auto& s : preprocessed.includeSites) {
        if (s.file == path) continue;
        if (m_sdb.fileIdFor(s.file) < 0) m_sdb.upsertFile(s.file, "");
        readFiles.push_back(s.file);
    }
    const auto previouslyRead = m_sdb.includedFilesOf(path);
    m_sdb.replaceFileIncludes(fid, readFiles);
    m_sdb.replaceIncludeDirectives(fid, preprocessed.includeDirectives);
    // Such a file goes altogether (plan.md §6.34 S2) unless it was last
    // compiled on its own (an inputs fingerprint): then its rows are its own.
    for (const auto& old : previouslyRead) {
        if (std::find(readFiles.begin(), readFiles.end(), old) != readFiles.end()) continue;
        if (!m_sdb.includersOf(old).empty()) continue;
        const auto stored = m_sdb.storedFile(old);
        if (stored && !stored->inputsFp.empty()) continue;
        m_sdb.removeFile(old);
    }

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
        parsed.checkTypes ? runTypeCheck(path, fid, text, walked.typeRefs, unitFiles)
                          : std::vector<ParseError>{};
    refreshTypeRefDiagnostics(fid, allIncluded, refreshedFiles);

    // Each file's identity as this compile read it (plan.md §6.34 S1), last:
    // the steps above may add rows for these files. An included file's rows
    // now come from this compile, so a fingerprint of its own is cleared --
    // compiled on its own next, it misses the cache. The primary's
    // fingerprint is what a later prepare() compares; none when an
    // `include failed, as the result then depends on a file this compile
    // couldn't name.
    std::vector<std::pair<std::string, std::string>> readHashes;
    for (const auto& s : preprocessed.includeSites) {
        if (s.file == path) continue;
        m_sdb.setFileIdentity(m_sdb.fileIdFor(s.file),
                              {s.hash, s.size, s.mtimeNs, s.statNs, ""});
        readHashes.emplace_back(s.file, s.hash);
    }
    auto primaryIdentity = parsed.primary;
    primaryIdentity.inputsFp = includeFailed(preprocessed)
        ? std::string{}
        : unitFingerprint(parsed.inputsKey, hash, std::move(readHashes));
    m_sdb.setFileIdentity(fid, primaryIdentity);

    txn.commit();
    if (includedFiles) *includedFiles = std::move(allIncluded);

    auto result = errsByFile[""];
    result.insert(result.end(), includeErrsByFile[""].begin(), includeErrsByFile[""].end());
    result.insert(result.end(), ppErrsByFile[""].begin(), ppErrsByFile[""].end());
    result.insert(result.end(), primaryContextDiags.begin(), primaryContextDiags.end());
    result.insert(result.end(), primaryCallDiags.begin(), primaryCallDiags.end());
    result.insert(result.end(), primaryTypeDiags.begin(), primaryTypeDiags.end());
    result.insert(result.end(), primaryUnresolvedDiags.begin(), primaryUnresolvedDiags.end());
    return result;
}

// The declared-type check (plan.md §6.21 step 3): each type name of the unit
// whose primary is `path` (file `fid`, compiled from `text`) writes at a
// declaration, checked once the whole unit is persisted, as 'reference'
// Warnings with the written name as subject. Every file in `unitFiles` has
// its old rows replaced, even with no references left. Returns the
// primary's.
std::vector<ParseError> CompilationController::runTypeCheck(
    const std::string& path, int64_t fid, const std::string& text,
    const std::vector<TypeRefRecord>& refs, const std::vector<std::string>& unitFiles)
{
    if (!m_typeChecker) return {};
    const auto start = std::chrono::steady_clock::now();

    // The unit's files' texts: the primary's, and each included file with
    // references, as the preprocessor read it.
    std::unordered_map<std::string, std::string> texts{{path, text}};
    for (const auto& ref : refs)
        if (!ref.file.empty() && !texts.count(ref.file))
            if (auto t = readFile(ref.file)) texts.emplace(ref.file, std::move(*t));
    const auto problems = m_typeChecker(m_sdb, path, texts, refs);

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

    m_sdb.clearDiagnostics("reference", unitFiles);
    size_t warnings = 0;
    for (const auto& [filePath, diags] : warningsByFile) {
        const int64_t fileId = filePath.empty() ? fid : knownFileId(m_sdb, filePath);
        const auto& subjects = subjectsByFile[filePath];
        for (size_t i = 0; i < diags.size(); ++i)
            m_sdb.appendDiagnostics(fileId, {diags[i]}, "reference", subjects[i]);
        warnings += diags.size();
    }
    if (m_log) {
        const auto ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
        *m_log << "[types]    " << path << ": " << refs.size() << " refs, " << warnings
               << " warnings, " << static_cast<int>(ms) << " ms\n";
        m_log->flush();
    }
    return warningsByFile[""];
}

// The missing-required-argument check (plan.md §6.23), run once every
// symbol of the unit is persisted, so a callee declared later in the same
// file, or in one of its `include's, resolves. Each call's callee is
// resolved against its file's text: `text` for the primary, the file on
// disk for an included one. `only`, when non-null, selects the calls to
// check (by index): their 'call' rows are replaced, the others' kept;
// otherwise every file in `unitFiles` has its 'call' rows replaced.
// `resolved[i]` is set to whether calls[i]'s callee resolved (when
// checked). Returns the primary's new rows.
std::vector<ParseError> CompilationController::runCallCheck(
    const std::string& path, int64_t fid, const std::string& text,
    const std::vector<CallRecord>& calls, const std::vector<std::string>& unitFiles,
    std::vector<bool>& resolved, const std::vector<size_t>* only)
{
    resolved.resize(calls.size(), false);
    if (!only) m_sdb.clearDiagnostics("call", unitFiles);
    if (!m_calleeResolver) return {};
    std::unordered_map<std::string, std::vector<size_t>> byFile;
    if (only)
        for (size_t i : *only) byFile[calls[i].file].push_back(i);
    else
        for (size_t i = 0; i < calls.size(); ++i) byFile[calls[i].file].push_back(i);
    std::vector<ParseError> primaryDiags;
    for (const auto& [filePath, indices] : byFile) {
        std::optional<std::string> incText;
        if (!filePath.empty() && !(incText = readFile(filePath))) continue;
        const int64_t fileId = filePath.empty() ? fid : knownFileId(m_sdb, filePath);
        std::vector<CallRecord> fileCalls;
        for (size_t i : indices) {
            fileCalls.push_back(calls[i]);
            if (only) m_sdb.deleteDiagnostic(fileId, calls[i].line, calls[i].column, "call");
        }
        const auto callees = m_calleeResolver(m_sdb, filePath.empty() ? path : filePath,
                                              filePath.empty() ? text : *incText, fileCalls);
        for (size_t k = 0; k < indices.size() && k < callees.size(); ++k)
            resolved[indices[k]] = callees[k].has_value();
        auto diags = checkMissingArguments(m_sdb, fileCalls, callees);
        if (diags.empty()) continue;
        m_sdb.appendDiagnostics(fileId, diags, "call");
        if (filePath.empty()) primaryDiags = std::move(diags);
    }
    return primaryDiags;
}

void CompilationController::recheckUnits(const std::vector<std::string>& units,
                                         const RecheckNames& names)
{
    const auto start = std::chrono::steady_clock::now();
    size_t calls = 0, refs = 0;
    for (const auto& unit : units) {
        const int64_t fid = m_sdb.fileIdFor(unit);
        if (fid < 0) continue;

        // Calls: an unresolved one whose name was declared, and any whose
        // name changed (a removed declaration, a port added or removed).
        std::vector<bool> resolved;
        const auto unitCalls = parseCalls(m_sdb.unitChecks(fid, "calls"), resolved);
        std::vector<size_t> callIdx;
        for (size_t i = 0; i < unitCalls.size(); ++i) {
            const auto& name = unitCalls[i].calleeName;
            if (namesOneOf(names.changed, name) || (!resolved[i] && namesOneOf(names.added, name)))
                callIdx.push_back(i);
        }
        // Type references: one whose name changed. A declaration added
        // clears warnings at the commit that adds it
        // (refreshTypeRefDiagnostics).
        std::vector<TypeRefRecord> unitRefs;
        if (const std::string types = m_sdb.unitChecks(fid, "types"); !types.empty())
            for (auto& r : parseTypeRefs(types))
                if (namesOneOf(names.changed, r.name)) unitRefs.push_back(std::move(r));
        if (callIdx.empty() && unitRefs.empty()) continue;

        const auto text = readFile(unit);
        if (!text) continue;
        std::vector<std::string> unitFiles{unit};
        for (auto& f : m_sdb.includedFilesOf(unit)) unitFiles.push_back(std::move(f));
        if (!callIdx.empty()) {
            runCallCheck(unit, fid, *text, unitCalls, unitFiles, resolved, &callIdx);
            m_sdb.replaceUnitChecks(fid, "calls", serializeCalls(unitCalls, resolved));
            calls += callIdx.size();
        }
        if (!unitRefs.empty() && m_typeChecker) {
            std::unordered_map<std::string, std::string> texts{{unit, *text}};
            for (const auto& ref : unitRefs)
                if (!ref.file.empty() && !texts.count(ref.file))
                    if (auto t = readFile(ref.file)) texts.emplace(ref.file, std::move(*t));
            const auto problems = m_typeChecker(m_sdb, unit, texts, unitRefs);
            for (size_t i = 0; i < unitRefs.size() && i < problems.size(); ++i) {
                const auto& ref = unitRefs[i];
                const int64_t fileId = ref.file.empty() ? fid : knownFileId(m_sdb, ref.file);
                m_sdb.deleteDiagnostic(fileId, ref.line, ref.column, "reference");
                if (!problems[i]) continue;
                ParseError diag{ref.line, ref.column, *problems[i]};
                diag.severity = 2;
                m_sdb.appendDiagnostics(fileId, {diag}, "reference",
                                        ref.qualifier.empty() ? ref.name
                                                              : ref.qualifier + "::" + ref.name);
            }
            refs += unitRefs.size();
        }
    }
    if (m_log) {
        const auto ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - start).count();
        *m_log << "[recheck]  " << units.size() << " units, " << calls << " calls, " << refs
               << " type refs, " << static_cast<int>(ms) << " ms\n";
        m_log->flush();
    }
}

void CompilationController::refreshContextDiagnostics(const std::vector<std::string>& files)
{
    for (const auto& file : files) {
        const int64_t fid = m_sdb.fileIdFor(file);
        if (fid >= 0) m_sdb.replaceDiagnostics(fid, contextDiagnostics(m_sdb, file), "context");
    }
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
