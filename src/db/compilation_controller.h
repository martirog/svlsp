#pragma once
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include "compiler/project_config.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <functional>
#include <iosfwd>
#include <optional>
#include <unordered_set>
#include <string>
#include <vector>

// Orchestrates the incremental compilation pipeline.
//
// On each compile(path, text) call:
//   • Compute the unit's inputs: the content hash of `text`, the config and
//     include context, and each file the last compile of `path` read
//     (stat data first, the content hash when it changed).
//   • If their fingerprint matches the one stored for `path` → return
//     cached diagnostics (no re-parse; plan.md §6.34 S1).
//   • Otherwise → run CompilerDirectiveStripper → SvPreprocessor →
//     SvTreeWalker, persist symbols + diagnostics, and record each file's
//     identity and the new fingerprint.
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
                                    bool forceRecompile = false,
                                    std::vector<std::string>* refreshedFiles = nullptr);

    // compile() in three steps, for a caller whose lock over the DB must not
    // be held across the parse (LanguageServer): prepare() and commit() use
    // the DB, parse() does not. compile() is exactly prepare, then -- unless
    // prepare() hit the cache -- parse and commit. A caller must not run
    // another compile between one job's prepare() and its commit().
    struct Prepared {
        std::string path;
        std::string text;
        std::string hash;
        std::optional<std::vector<ParseError>> cached{}; // a cache hit: compile()'s result
        SvPreprocessor preprocessor;                     // seeded with config + include context
        std::string initialScope{};
        bool checkTypes{false}; // compiled under a project config (plan.md §6.21 step 3)
        // What the result depends on besides the files' contents: the
        // config and include context (plan.md §6.34 S1).
        std::string inputsKey{};
    };
    struct Parsed {
        std::string path;
        std::string text;
        std::string hash;
        PreprocessorResult preprocessed;
        WalkResult walked;
        bool checkTypes{false};
        std::string inputsKey{};
        // The primary file's identity: `hash`, plus stat data when the text
        // was the file's own (size -1 for an unsaved buffer).
        SymbolDatabase::StoredFile primary{};
        // What the parse logged, when it was asked to buffer it (a parse on
        // another thread, plan.md §6.34 S8); commit() writes it.
        std::string bufferedLog{};
    };
    Prepared prepare(const std::string& path, const std::string& text,
                     const ProjectConfig* config = nullptr, bool forceRecompile = false);
    Parsed parse(Prepared&& job) const;
    // parse() for another thread (plan.md §6.34 S8): thread-safe when the
    // walker is; its log lines go to Parsed::bufferedLog, not the log.
    Parsed parseBuffered(Prepared&& job) const;
    // Whether `parsed`, prepared before other units were committed, is
    // still what a prepare() now would parse: the same inputs key (config,
    // include context and its macros). Its files' contents are the disk's
    // either way.
    bool parseStillValid(const Parsed& parsed, const ProjectConfig* config) const;
    // Persists `parsed` and returns compile()'s result; `includedFiles` as
    // in compile(). `refreshedFiles`: the files outside this compile whose
    // 'reference' diagnostics this compile cleared (it declared what they
    // name), to republish.
    std::vector<ParseError> commit(Parsed&& parsed,
                                   std::vector<std::string>* includedFiles = nullptr,
                                   std::vector<std::string>* refreshedFiles = nullptr);

    // Whether doc comments (plan.md §6.31) are extracted and stored; on by
    // default. Off leaves symbol_docs empty and macros.doc "", saving the
    // space they take (initializationOptions.svlsp.docComments, and
    // `--build-db --no-doc-comments`). Applies to files compiled from now on.
    void setCollectDocs(bool collect) { m_collectDocs = collect; }

    // Resolves each of `calls` (all in `path`, whose source text is `text`)
    // to the Function/Task it invokes, or nullopt; result[i] is calls[i]'s.
    using CalleeResolver = std::function<std::vector<std::optional<SymbolRow>>(
        SymbolDatabase&, const std::string& path, const std::string& text,
        const std::vector<CallRecord>& calls)>;

    // The resolver the missing-required-argument check (plan.md §6.23) finds
    // callees with -- in practice lsp/symbol_resolution.h's resolveCallees,
    // which svlsp_db can't link itself. Unset (the default), the check
    // doesn't run.
    void setCalleeResolver(CalleeResolver resolver) { m_calleeResolver = std::move(resolver); }

    // For each of `refs` (declared type names of the unit whose primary file
    // is `path`, in the order they are written; `texts` has the source text
    // of the files at hand, by path): nullopt if the type is visible there,
    // else the warning's message.
    using TypeChecker = std::function<std::vector<std::optional<std::string>>(
        SymbolDatabase&, const std::string& path,
        const std::unordered_map<std::string, std::string>& texts,
        const std::vector<TypeRefRecord>& refs)>;

    // The checker the declared-type check (plan.md §6.21 step 3, 'reference'
    // diagnostics, Warnings) uses -- in practice lsp/symbol_resolution.h's
    // checkTypeRefs. It runs only on files compiled under a project config;
    // unset (the default), it doesn't run.
    void setTypeChecker(TypeChecker checker) { m_typeChecker = std::move(checker); }

    // Runs the parse (SvTreeWalker::walk's signature). Unset, the walk runs
    // in this process; the server sets a ParseWorker's (plan.md §6.33 C1,
    // compiler/parse_worker.h) so ANTLR's memory lives in a child process.
    using Walker = std::function<WalkResult(const std::string& source,
                                            const std::vector<SourceLine>& sourceMap,
                                            bool collectDocs, const std::string& initialScope)>;
    void setWalker(Walker walker) { m_walker = std::move(walker); }
    const Walker& walker() const { return m_walker; } // unset = in-process

    // The walk every parse of this controller goes through (also
    // LibraryResolver's pre-parse of -v files).
    WalkResult walk(const std::string& source, const std::vector<SourceLine>& sourceMap,
                    bool collectDocs, const std::string& initialScope) const;

    // --- Refresh support (plan.md §6.34 S2) ---------------------------------

    // Whether compiling `path` from its file on disk under `config` would hit
    // the cache: prepare()'s check, with the primary's hash taken from its
    // stat data when that still matches.
    bool isUpToDate(const std::string& path, const ProjectConfig* config) const;

    // The fingerprint of what every unit compiled under `config` depends on
    // besides its files: defines, -y / -v / +libext+, the mode, and the
    // settings and checks of this controller. Not the file list or the
    // include dirs (those count per unit).
    std::string configFingerprint(const ProjectConfig* config) const;

    // The declared names a refresh changed: `added`, names newly declared;
    // `changed`, names whose declaration went away or changed -- a removed
    // symbol's name and its scope's last segment, and the function of a
    // port that was added or removed.
    struct RecheckNames {
        std::unordered_set<std::string> added, changed;
        bool empty() const { return added.empty() && changed.empty(); }
    };

    // Re-runs, from each of `units`' stored inputs (unit_checks) and its
    // files' texts on disk, the checks whose result `names` can change
    // (plan.md §6.34 D3): a call whose callee was unresolved and is now
    // declared, or whose callee's name changed; a declared-type reference
    // whose name changed. What a refresh does for the
    // units it didn't recompile. Their rows are replaced one by one; the
    // other checks' rows stay.
    void recheckUnits(const std::vector<std::string>& units, const RecheckNames& names);

    // Recomputes the "`include'd in N scopes" Information row of each of
    // `files`, whose include sites changed outside a compile of them.
    void refreshContextDiagnostics(const std::vector<std::string>& files);

private:
    SymbolDatabase& m_sdb;
    std::ostream*   m_log;
    bool            m_collectDocs{true};
    CalleeResolver  m_calleeResolver;
    TypeChecker     m_typeChecker;

    std::vector<ParseError> runTypeCheck(const std::string& path, int64_t fid,
                                         const std::string& text,
                                         const std::vector<TypeRefRecord>& refs,
                                         const std::vector<std::string>& unitFiles);
    std::vector<ParseError> runCallCheck(const std::string& path, int64_t fid,
                                         const std::string& text,
                                         const std::vector<CallRecord>& calls,
                                         const std::vector<std::string>& unitFiles,
                                         std::vector<bool>& resolved,
                                         const std::vector<size_t>* only);
    Parsed parseTo(Prepared&& job, std::ostream* log) const;
    std::string inputsKeyFor(const std::string& path, const ProjectConfig* config,
                             std::optional<IncludeSiteRow>* context,
                             std::vector<MacroRow>* contextMacros) const;
    void refreshTypeRefDiagnostics(int64_t fid, const std::vector<std::string>& included,
                                   std::vector<std::string>* refreshedFiles);
    Walker          m_walker;

    static std::string hashContent(const std::string& text);
};
