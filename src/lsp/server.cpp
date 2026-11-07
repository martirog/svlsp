#include "server.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"
#include "lsp/library_db_builder.h"
#include "lsp/load_order.h"
#include "db/project_compiler.h"
#include "db/library_check.h"
#include "compiler/file_utils.h"
#include "compiler/sv_preprocessor.h"
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <future>
#include <thread>
#include <iostream>
#include <optional>
#include <lsp/messages.h>
#include <lsp/process.h>
#include <lsp/uri.h>

LanguageServer::LanguageServer(lsp::io::Stream& io, std::ostream* logStream,
                               std::ostream* requestLog)
    : m_db{":memory:"}
    , m_symbolDb{m_db}
    , m_compiler{m_symbolDb, logStream}
    , m_projects{m_compiler, m_symbolDb}
    , m_connection{io}
    , m_messageHandler{m_connection}
    , m_diagnostics{m_messageHandler}
    , m_requestLog{requestLog}
    , m_queue{[this](const CompileQueue::Job& job) { runJob(job); }}
    , m_debouncer{[this](const std::string& uriStr) {
          const lsp::DocumentUri uri{lsp::Uri::parse(uriStr)};
          enqueueCompile(std::string{uri.path()}, CompileQueue::kOpenDocument);
      }}
    , m_refreshDebouncer{[this](const std::string&) { refreshAll(false); },
                         std::chrono::milliseconds{500}}
{
    m_db.initSchema();
    m_compiler.setCalleeResolver(resolveCallees);
    m_compiler.setTypeChecker(checkTypeRefs);
    // A newly discovered project loads in the background (plan.md §6.34 S4),
    // into its DB file (S3): the one it names, else one in the XDG cache
    // (S3b); "database": ":memory:" keeps it in memory.
    m_projects.deferLoading([this](const std::string& configPath) {
        if (const ProjectConfig* c = m_projects.mutableConfig(configPath)) {
            const std::string db = c->database.empty()
                                       ? ProjectRegistry::defaultDatabasePath(configPath)
                                       : c->database;
            if (!db.empty() && db != ":memory:") openProjectDb(db);
        }
        m_queue.push({.kind = CompileQueue::Job::Kind::LoadProject, .key = configPath},
                     CompileQueue::kProjectSetup);
    });
    registerHandlers();
}

int LanguageServer::run()
{
    try {
        while (m_state.isRunning())
            m_messageHandler.processIncomingMessages();
    } catch (const std::exception& e) {
        std::cerr << "svlsp: fatal error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

lsp::Array<lsp::Diagnostic> LanguageServer::toDiagnostics(const std::vector<ParseError>& errs)
{
    lsp::Array<lsp::Diagnostic> diags;
    for (const auto& err : errs)
        diags.push_back(DiagnosticsPublisher::buildDiagnostic(err));
    return diags;
}

lsp::Array<lsp::Diagnostic> LanguageServer::storedDiagnostics(const std::string& path,
                                                           bool holdBack)
{
    std::vector<ParseError> errs;
    for (const auto& r : m_symbolDb.diagnosticsForFile(path)) {
        if (holdBack && (r.source == "reference" || r.source == "call" || r.source == "library"))
            continue;
        errs.push_back({r.line, r.col, r.message, {}, r.severity});
    }
    return toDiagnostics(errs);
}

std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>>
LanguageServer::collectIncludedDiagnostics(const std::vector<std::string>& paths, bool holdBack)
{
    std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>> result;
    for (const auto& incPath : paths)
        result.emplace_back(pathToUri(incPath), storedDiagnostics(incPath, holdBack));
    return result;
}

std::optional<LanguageServer::CompileOutcome> LanguageServer::compileCurrent(
    const lsp::DocumentUri& uri, bool openOnly, bool forceRecompile, const char* what,
    const std::string& project)
{
    std::lock_guard serial{m_compileMutex};
    const std::string path{uri.path()};
    const auto start = TimedLock::Clock::now();
    TimedLock::Clock::time_point acquired;
    CompileOutcome out;
    std::optional<CompilationController::Prepared> job;
    // A unit of a project being loaded: its declarations before the compile
    // (ProjectRefresher::unitCompiled).
    auto loadOf = [&]() -> ProjectLoad* {
        for (auto& [configPath, load] : m_loads)
            if (load.pending.count(path)) return &load;
        return nullptr;
    };
    std::vector<SymbolDatabase::Declaration> declarationsBefore;
    {
        std::lock_guard lock{m_dataMutex};
        acquired = TimedLock::Clock::now();
        std::optional<std::string> text;
        if (auto doc = m_store.find(uri)) {
            out.version = doc->version;
            text        = std::move(doc->text);
        } else if (!openOnly) {
            text = readFile(path);
        }
        if (!text) return std::nullopt;
        if (loadOf()) declarationsBefore = m_symbolDb.declarationsOfUnit(path);
        const ProjectConfig* config =
            project.empty() ? m_projects.configFor(path) : m_projects.mutableConfig(project);
        job = m_compiler.prepare(path, *text, config, forceRecompile);
    }

    // The parse runs without the data mutex: requests are answered from the
    // previous compile's state meanwhile, and didChange updates the store.
    std::optional<CompilationController::Parsed> parsed;
    if (!job->cached) parsed = m_compiler.parse(std::move(*job));

    TimedLock lock{m_dataMutex, m_requestLog, what, m_requestLog ? path : std::string{}};
    lock.reportSince(start, acquired);
    std::vector<std::string> includedFiles, refreshedFiles;
    auto primary = job->cached ? *job->cached
                               : m_compiler.commit(std::move(*parsed), &includedFiles,
                                                   &refreshedFiles);
    if (!job->cached) ++m_commitGeneration;
    if (ProjectLoad* load = loadOf()) {
        ProjectRefresher::unitCompiled(load->progress, load->plan, path, declarationsBefore,
                                       m_symbolDb);
        load->pending.erase(path);
        const size_t done = load->total - load->pending.size();
        if (!load->progressToken.empty() && load->total > 0)
            reportProgress(load->progressToken,
                           std::to_string(done) + "/" + std::to_string(load->total) + " units",
                           static_cast<unsigned>(100 * done / load->total));
    }
    // While a project indexes, what depends on other units is held back
    // (plan.md §6.34 S4): it is published once the load ends.
    const bool holdBack = indexing();
    out.diags = holdBack ? storedDiagnostics(path, true) : toDiagnostics(primary);
    // Files whose declared-type warnings this compile cleared (plan.md
    // §6.21 step 3) are republished with the included ones.
    includedFiles.insert(includedFiles.end(), refreshedFiles.begin(), refreshedFiles.end());
    out.includedDiags = collectIncludedDiagnostics(includedFiles, holdBack);
    return out;
}

void LanguageServer::publish(const lsp::DocumentUri& uri, CompileOutcome&& out)
{
    {
        std::lock_guard l{m_publishMutex};
        m_published.insert(std::string{uri.path()});
        for (const auto& [incUri, incDiags] : out.includedDiags)
            m_published.insert(std::string{incUri.path()});
    }
    m_diagnostics.publish(uri, out.version, out.diags);
    for (auto& [incUri, incDiags] : out.includedDiags)
        m_diagnostics.publish(incUri, std::nullopt, std::move(incDiags));
}

void LanguageServer::enqueueCompile(const std::string& path, int rank, bool force, bool openOnly,
                                    bool publishAll)
{
    m_queue.push({.kind = CompileQueue::Job::Kind::Unit, .key = path, .force = force,
                  .openOnly = openOnly, .publishAll = publishAll},
                 rank);
}

LanguageServer::Request::Request(LanguageServer& server, const char* what, std::string where)
    : m_reader{server.m_readerReady.load()}
    , m_lock{m_reader ? server.m_readMutex : server.m_dataMutex, server.m_requestLog, what,
             std::move(where)}
    , m_db{m_reader ? &server.m_readDb : &server.m_symbolDb}
{
    if (!m_reader) return;
    server.syncReader();
    try {
        m_db->database().execute("BEGIN");
    } catch (const std::exception&) {
    }
}

LanguageServer::Request::~Request()
{
    if (!m_reader) return;
    try {
        m_db->database().execute("COMMIT");
    } catch (const std::exception&) {
    }
}

void LanguageServer::awaitCompile(const std::string& path)
{
    std::lock_guard l{m_awaitMutex};
    m_awaiting.insert(path);
}

void LanguageServer::compiled(const std::string& path)
{
    {
        std::lock_guard l{m_awaitMutex};
        if (!m_awaiting.erase(path)) return;
    }
    m_awaitCv.notify_all();
}

void LanguageServer::waitForDocument(const lsp::DocumentUri& uri)
{
    const std::string path{uri.path()};
    std::unique_lock l{m_awaitMutex};
    m_awaitCv.wait(l, [&] { return !m_awaiting.count(path) || m_dbWaiting.load(); });
}

bool LanguageServer::awaiting(const lsp::DocumentUri& uri)
{
    std::lock_guard l{m_awaitMutex};
    return m_awaiting.count(std::string{uri.path()}) > 0 && !m_dbWaiting.load();
}

void LanguageServer::syncReader()
{
    const uint64_t generation = m_commitGeneration.load();
    if (generation == m_readGeneration) return;
    std::vector<std::string> attached, reattach;
    {
        std::lock_guard l{m_readSyncMutex};
        attached = m_readerAttached;
        reattach.swap(m_readerReattach);
    }
    try {
        m_readDb.attachLibraryDbs(attached);
        for (const auto& db : reattach) m_readDb.reattachLibraryDb(db);
    } catch (const std::exception& e) {
        std::cerr << "svlsp: reader: " << e.what() << '\n';
    }
    m_readDb.clearCaches();
    m_readGeneration = generation;
}

void LanguageServer::noteAttached(const std::vector<std::string>& libraryDbs,
                                  const std::vector<std::string>& reattach)
{
    {
        std::lock_guard l{m_readSyncMutex};
        for (const auto& db : reattach) m_readerReattach.push_back(db);
        for (const auto& db : libraryDbs)
            if (std::find(m_readerAttached.begin(), m_readerAttached.end(), db) ==
                m_readerAttached.end())
                m_readerAttached.push_back(db);
    }
    ++m_commitGeneration;
}

void LanguageServer::openProjectDb(const std::string& path)
{
    if (m_dbMode != DbMode::Memory) {
        if (path != m_dbPath)
            std::cerr << "svlsp: already using the DB '" << m_dbPath << "'; '" << path
                      << "' is not opened (one DB per server)\n";
        return;
    }
    if (!m_symbolDb.allFilePaths().empty()) {
        std::cerr << "svlsp: files were compiled into memory already; the DB '" << path
                  << "' is not opened\n";
        return;
    }
    try {
        if (const auto parent = std::filesystem::path(path).parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);
        m_dbPath = path;
        if (m_dbLock.acquire(path, "server")) {
            becomeWriter();
            return;
        }
        int pid = 0;
        if (DbLock::holder(path, &pid) == DbLock::Holder::Builder) {
            // A --build-db is writing it: serve what it has committed so far,
            // and take over once it is done.
            if (std::filesystem::exists(path)) {
                m_db.reopen(path, /*readOnly=*/true);
                m_symbolDb.connectionChanged();
            }
            m_dbMode = DbMode::Waiting;
            m_dbWaiting = true;
            m_awaitCv.notify_all();
            std::cerr << "svlsp: svlsp --build-db (pid " << pid << ") is writing '" << path
                      << "'; serving from it until it is done\n";
            return;
        }
        // Another server writes it (D2): a copy in memory, never written.
        m_db.copyFrom(path);
        m_db.initSchema();
        checkDbVersion();
        m_symbolDb.connectionChanged();
        m_dbMode = DbMode::Copy;
        std::cerr << "svlsp: svlsp pid " << pid << " writes '" << path
                  << "'; working on a copy in memory\n";
    } catch (const std::exception& e) {
        std::cerr << "svlsp: can't use the DB '" << path << "': " << e.what()
                  << "; working in memory\n";
        m_dbLock.release();
        m_db.reopen(":memory:");
        m_db.initSchema();
        m_symbolDb.connectionChanged();
        m_dbMode = DbMode::Memory;
    }
}

void LanguageServer::becomeWriter()
{
    m_db.reopen(m_dbPath);
    m_db.useWal();
    m_db.initSchema();
    checkDbVersion();
    m_symbolDb.connectionChanged();
    m_dbMode = DbMode::Writer;
    m_dbWaiting = false;
    // The reader (plan.md §6.34 S5). Requests start using it at their next
    // one; until then they use the writer under m_dataMutex, as here.
    if (!m_readerReady) {
        try {
            m_readConn.reopen(m_dbPath, /*readOnly=*/true);
            m_readDb.connectionChanged();
            {
                std::lock_guard l{m_readSyncMutex};
                m_readerAttached.clear();
            }
            ++m_commitGeneration;
            m_readerReady = true;
        } catch (const std::exception& e) {
            std::cerr << "svlsp: no reader connection on '" << m_dbPath << "': " << e.what()
                      << '\n';
        }
    }
}

void LanguageServer::checkDbVersion()
{
    if (m_buildVersion.empty() || m_symbolDb.builtByVersion() == m_buildVersion) return;
    m_symbolDb.resetAllFiles();
    m_symbolDb.setBuiltByVersion(m_buildVersion);
}

void LanguageServer::waitForWriter()
{
    std::string path;
    {
        std::lock_guard lock{m_dataMutex};
        if (m_dbMode != DbMode::Waiting) return;
        path = m_dbPath;
    }
    m_dbLock.acquire(path, "server", /*wait=*/true);
    std::lock_guard lock{m_dataMutex};
    try {
        becomeWriter();
    } catch (const std::exception& e) {
        std::cerr << "svlsp: can't open the DB '" << path << "': " << e.what() << '\n';
    }
}

void LanguageServer::runJob(const CompileQueue::Job& job)
{
    waitForWriter();
    switch (job.kind) {
    case CompileQueue::Job::Kind::Unit:          compileUnit(job); break;
    case CompileQueue::Job::Kind::LoadProject:   loadProject(job.key); break;
    case CompileQueue::Job::Kind::FinishProject: finishProject(job.key); break;
    case CompileQueue::Job::Kind::Refresh:       refreshProject(job.key, job.force); break;
    }
}

void LanguageServer::compileUnit(const CompileQueue::Job& job)
{
    const lsp::DocumentUri uri = pathToUri(job.key);
    std::optional<CompileOutcome> out;
    try {
        out = compileCurrent(uri, job.openOnly, job.force, job.force ? "recompile" : "compile",
                             job.project);
    } catch (...) {
        compiled(job.key);
        throw;
    }
    compiled(job.key);
    if (!out) return;
    if (!job.publishAll) {
        // A load's unit: only what is open or was published before.
        bool open = false;
        {
            std::lock_guard lock{m_dataMutex};
            open = m_store.contains(uri);
            std::lock_guard l{m_publishMutex};
            auto& inc = out->includedDiags;
            inc.erase(std::remove_if(inc.begin(), inc.end(),
                                     [&](const auto& d) {
                                         return !m_store.contains(d.first) &&
                                                !m_published.count(std::string{d.first.path()});
                                     }),
                      inc.end());
            open = open || m_published.count(job.key);
        }
        if (!open) {
            for (auto& [incUri, incDiags] : out->includedDiags)
                m_diagnostics.publish(incUri, std::nullopt, std::move(incDiags));
            return;
        }
    }
    publish(uri, std::move(*out));
}

void LanguageServer::refreshAll(bool rehash)
{
    std::vector<std::string> configs;
    {
        std::lock_guard lock{m_dataMutex};
        configs = m_projects.configPaths();
    }
    for (const auto& c : configs)
        m_queue.push({.kind = CompileQueue::Job::Kind::Refresh, .key = c, .force = rehash},
                     CompileQueue::kProjectSetup);
}

void LanguageServer::refreshProject(const std::string& configPath, bool rehash)
{
    {
        std::lock_guard lock{m_dataMutex};
        if (m_loads.count(configPath)) {
            // Its load (or last refresh) is still running: after it.
            m_queue.push({.kind = CompileQueue::Job::Kind::Refresh, .key = configPath,
                          .force = rehash},
                         CompileQueue::kAfterLoad);
            return;
        }
    }
    // The config may have changed too (a checkout): reread it.
    ProjectConfig fresh;
    try {
        fresh = ProjectRegistry::parseConfig(configPath);
    } catch (const std::exception& e) {
        std::cerr << "svlsp: can't reread '" << configPath << "': " << e.what() << '\n';
        return;
    }
    {
        std::lock_guard lock{m_dataMutex};
        ProjectConfig* c = m_projects.mutableConfig(configPath);
        if (!c) return;
        *c = std::move(fresh);
    }
    loadProject(configPath, rehash);
}

void LanguageServer::loadProject(const std::string& configPath, bool rehash)
{
    ProjectConfig config;
    {
        std::lock_guard lock{m_dataMutex};
        const ProjectConfig* c = m_projects.mutableConfig(configPath);
        if (!c) return;
        config = *c;
    }
    const std::string token = beginProgress("svlsp: indexing", "preparing");

    // Library DBs still to build, without the data mutex: requests are
    // answered meanwhile (their symbols are missing until this is done).
    // A cache svlsp owns that went stale is rebuilt here; someone else's
    // archive is only reported, once (plan.md §6.34 S7).
    std::vector<std::string> rebuilt;
    try {
        LibraryDbBuilder::resolveLibraryDbSources(config, nullptr, "", m_compiler.walker(),
                                                  &rebuilt);
    } catch (const std::exception& e) {
        std::cerr << "svlsp: " << e.what() << '\n';
    }
    {
        std::set<std::string> owned;
        for (const auto& src : config.libraryDbSources) owned.insert(src.cachePath);
        for (const auto& lib : config.libraryDbs) {
            if (owned.count(lib) || m_staleWarned.count(lib)) continue;
            const LibraryStatus status = checkLibraryDb(lib);
            if (!status.stale()) continue;
            m_staleWarned.insert(lib);
            const std::string msg = "svlsp: " + lib + " is stale: " + status.describe() +
                                    " (rebuild it with svlsp --build-db)";
            std::cerr << msg << '\n';
            m_messageHandler.sendNotification<lsp::notifications::Window_ShowMessage>(
                lsp::ShowMessageParams{.type = lsp::MessageType::Warning, .message = msg});
        }
    }

    const std::string key = std::filesystem::absolute(configPath).lexically_normal().string();
    std::vector<std::string> units, force, includeDirs;
    std::vector<std::pair<std::string, std::string>> openTexts; // path, text
    std::set<std::string> knownIncluders; // units the DB says include an open document
    {
        std::lock_guard lock{m_dataMutex};
        ProjectConfig* c = m_projects.mutableConfig(configPath);
        if (!c) return;
        *c = std::move(config);
        ProjectCompiler::attachLibraries(*c, m_symbolDb);
        for (const auto& lib : rebuilt) m_symbolDb.reattachLibraryDb(lib);
        noteAttached(c->libraryDbs, rebuilt);
        ProjectLoad load;
        load.plan = ProjectRefresher::plan(key, *c, m_compiler, m_symbolDb, rehash);
        load.plan.svlspVersion = m_buildVersion;
        load.progress = ProjectRefresher::begin(load.plan, m_symbolDb);
        ++m_commitGeneration;
        load.pending.insert(load.plan.compile.begin(), load.plan.compile.end());
        load.total = load.pending.size();
        load.progressToken = token;
        units = load.plan.compile;
        force = load.plan.force;
        m_loads[configPath] = std::move(load);
        m_indexing = true;
        for (const auto& uri : m_store.uris())
            if (auto doc = m_store.find(uri)) {
                openTexts.emplace_back(std::string{uri.path()}, std::move(doc->text));
                for (auto& inc : m_symbolDb.includersOf(openTexts.back().first))
                    knownIncluders.insert(std::move(inc));
            }
        includeDirs = c->includeDirs;
    }

    // The order (plan.md §6.34 S4), from a text pre-scan: the units that
    // `include an open document, then open documents (already queued), the
    // units declaring what open documents name, then the rest in config
    // order.
    // A unit includes an open document when the DB says so, or when the
    // document is in the closure of its `include's (resolved as the
    // preprocessor would, each file scanned once).
    std::set<std::string> openPaths, referenced;
    for (const auto& [path, text] : openTexts) {
        openPaths.insert(path);
        for (auto& r : scanSource(text).references) referenced.insert(std::move(r));
    }
    std::unordered_map<std::string, SourceScan> scans;
    auto scanOf = [&](const std::string& file) -> const SourceScan& {
        auto it = scans.find(file);
        if (it == scans.end()) {
            const auto text = readFile(file);
            it = scans.emplace(file, text ? scanSource(*text) : SourceScan{}).first;
        }
        return it->second;
    };
    auto includesOpenDocument = [&](const std::string& unit) {
        if (knownIncluders.count(unit)) return true;
        std::set<std::string> seen{unit};
        std::vector<std::string> todo{unit};
        while (!todo.empty()) {
            const std::string file = std::move(todo.back());
            todo.pop_back();
            for (const auto& name : scanOf(file).includes) {
                const std::string resolved = resolveInclude(name, file, includeDirs);
                if (resolved.empty() || !seen.insert(resolved).second) continue;
                if (openPaths.count(resolved)) return true;
                todo.push_back(resolved);
            }
        }
        return false;
    };
    for (const auto& unit : units) {
        int rank = CompileQueue::kRest;
        if (!openTexts.empty()) {
            const SourceScan& scan = scanOf(unit);
            const bool includesOpen = includesOpenDocument(unit);
            const bool declaresNamed =
                std::any_of(scan.declares.begin(), scan.declares.end(),
                            [&](const std::string& n) { return referenced.count(n) > 0; });
            rank = includesOpen ? CompileQueue::kOpenUnit
                 : declaresNamed ? CompileQueue::kDeclarers
                                 : CompileQueue::kRest;
        }
        m_queue.push({.kind = CompileQueue::Job::Kind::Unit, .key = unit,
                      .force = std::find(force.begin(), force.end(), unit) != force.end(),
                      .openOnly = false, .publishAll = false, .project = configPath},
                     rank);
    }
    m_queue.push({.kind = CompileQueue::Job::Kind::FinishProject, .key = configPath},
                 CompileQueue::kProjectFinish);
}

void LanguageServer::finishProject(const std::string& configPath)
{
    std::vector<std::tuple<lsp::DocumentUri, std::optional<int>, lsp::Array<lsp::Diagnostic>>>
        toPublish;
    std::string token;
    size_t units = 0;
    {
        std::lock_guard lock{m_dataMutex};
        const auto it = m_loads.find(configPath);
        if (it == m_loads.end()) return;
        ProjectLoad load = std::move(it->second);
        const ProjectConfig* c = m_projects.mutableConfig(configPath);
        const auto result = c ? ProjectRefresher::finish(std::move(load.progress), load.plan, *c,
                                                         m_compiler, m_symbolDb)
                              : ProjectRefresher::Result{};
        m_loads.erase(it);
        m_indexing = !m_loads.empty();
        ++m_commitGeneration;
        token = load.progressToken;
        units = load.total;

        // What was held back, and what the refresh changed: every open
        // document, and the changed files published before.
        const bool holdBack = indexing();
        std::set<std::string> paths;
        for (const auto& uri : m_store.uris()) paths.insert(std::string{uri.path()});
        {
            std::lock_guard l{m_publishMutex};
            for (const auto& p : result.changedDiagnostics)
                if (m_published.count(p)) paths.insert(p);
        }
        for (const auto& p : paths) {
            const lsp::DocumentUri uri = pathToUri(p);
            std::optional<int> version;
            if (auto doc = m_store.find(uri)) version = doc->version;
            toPublish.emplace_back(uri, version, storedDiagnostics(p, holdBack));
        }
    }
    for (auto& [uri, version, diags] : toPublish) {
        {
            std::lock_guard l{m_publishMutex};
            m_published.insert(std::string{uri.path()});
        }
        m_diagnostics.publish(uri, version, std::move(diags));
    }
    endProgress(token, std::to_string(units) + " units");
}

std::string LanguageServer::beginProgress(const std::string& title, const std::string& message)
{
    if (!m_state.workDoneProgressSupported()) return "";
    std::string token;
    {
        std::lock_guard l{m_progressMutex};
        token = "svlsp-indexing-" + std::to_string(m_nextProgressToken++);
    }
    m_messageHandler.sendRequest<lsp::requests::Window_WorkDoneProgress_Create>(
        lsp::WorkDoneProgressCreateParams{.token = token},
        [this, token, title, message](lsp::Window_WorkDoneProgress_CreateResult&&) {
            std::optional<std::string> endedWith;
            {
                std::lock_guard l{m_progressMutex};
                if (auto it = m_progressEndedEarly.find(token); it != m_progressEndedEarly.end()) {
                    endedWith = it->second;
                    m_progressEndedEarly.erase(it);
                } else {
                    m_progressReady.insert(token);
                }
            }
            m_messageHandler.sendNotification<lsp::notifications::Progress>(
                lsp::ProgressParams{.token = token,
                                    .value = lsp::toJson(lsp::WorkDoneProgressBegin{
                                        .title = title, .message = message, .percentage = 0})});
            // It ended before the client answered: end it now, after its begin.
            if (endedWith)
                m_messageHandler.sendNotification<lsp::notifications::Progress>(
                    lsp::ProgressParams{.token = token,
                                        .value = lsp::toJson(lsp::WorkDoneProgressEnd{
                                            .message = *endedWith})});
        });
    return token;
}

void LanguageServer::reportProgress(const std::string& token, const std::string& message,
                                    unsigned percent)
{
    if (token.empty()) return;
    {
        std::lock_guard l{m_progressMutex};
        if (!m_progressReady.count(token)) return;
    }
    m_messageHandler.sendNotification<lsp::notifications::Progress>(lsp::ProgressParams{
        .token = token,
        .value = lsp::toJson(lsp::WorkDoneProgressReport{.message = message, .percentage = percent})});
}

void LanguageServer::endProgress(const std::string& token, const std::string& message)
{
    if (token.empty()) return;
    {
        std::lock_guard l{m_progressMutex};
        // Not acknowledged yet: the end goes out after the begin, once the
        // client answers (beginProgress).
        if (!m_progressReady.erase(token)) {
            m_progressEndedEarly[token] = message;
            return;
        }
    }
    m_messageHandler.sendNotification<lsp::notifications::Progress>(lsp::ProgressParams{
        .token = token, .value = lsp::toJson(lsp::WorkDoneProgressEnd{.message = message})});
}

std::string LanguageServer::requestWhere(const lsp::DocumentUri& uri,
                                         const lsp::Position& pos) const
{
    if (!m_requestLog) return {};
    return std::string{uri.path()} + ':' + std::to_string(pos.line) + ':' +
           std::to_string(pos.character);
}

std::optional<std::string> LanguageServer::currentTextFor(const std::string& path)
{
    if (auto doc = m_store.find(pathToUri(path))) return std::move(doc->text);
    return readFile(path);
}

void LanguageServer::registerHandlers()
{
    m_messageHandler
        .add<lsp::requests::Initialize>(
            [this](lsp::InitializeParams&& params) {
                auto result = m_state.handleInitialize(std::move(params));
                std::lock_guard lock{m_dataMutex};
                m_projects.setExplicitConfigPath(m_state.explicitProjectConfigPath());
                m_compiler.setCollectDocs(m_state.docCommentsEnabled());
                // The same string --build-db records (LibraryDbBuilder::build).
                m_buildVersion = m_version.empty() || m_state.docCommentsEnabled()
                    ? m_version
                    : m_version + " (no doc comments)";
                if (!m_state.databasePath().empty() && m_state.databasePath() != ":memory:")
                    openProjectDb(m_state.databasePath());
                return result;
            })
        .add<lsp::notifications::Initialized>(
            [this](lsp::notifications::Initialized::Params&&) {
                m_state.handleInitialized();
                // Watch the sources and configs, so a checkout outside the
                // editor refreshes the project (plan.md §6.34 S6).
                if (!m_state.watchedFilesSupported()) return;
                lsp::DidChangeWatchedFilesRegistrationOptions watch;
                for (const char* glob : {"**/*.{sv,svh,v,vh,svi,f}", "**/.svlsp.json",
                                         "**/svlsp.json"})
                    watch.watchers.push_back(lsp::FileSystemWatcher{.globPattern = glob});
                m_messageHandler.sendRequest<lsp::requests::Client_RegisterCapability>(
                    lsp::RegistrationParams{.registrations = {lsp::Registration{
                        .id = "svlsp-watched-files",
                        .method = "workspace/didChangeWatchedFiles",
                        .registerOptions = lsp::toJson(std::move(watch))}}},
                    [](lsp::Client_RegisterCapabilityResult&&) {});
            })
        .add<lsp::notifications::Workspace_DidChangeWatchedFiles>(
            [this](lsp::DidChangeWatchedFilesParams&& params) {
                // An open document's buffer is the truth: its events don't
                // count (lsp-mode sends didChange when it reverts one).
                bool relevant = false;
                {
                    std::lock_guard lock{m_dataMutex};
                    for (const auto& change : params.changes)
                        if (!m_store.contains(change.uri)) relevant = true;
                }
                if (relevant) m_refreshDebouncer.schedule("refresh");
            })
        .add<lsp::requests::Workspace_ExecuteCommand>(
            [this](lsp::ExecuteCommandParams&& params)
                -> lsp::AsyncRequestResult<lsp::requests::Workspace_ExecuteCommand> {
                if (params.command == "svlsp.waitForIndexing")
                    // Off the message thread: requests keep being answered.
                    return std::async(std::launch::async, [this] {
                        auto documentsPending = [this] {
                            std::lock_guard l{m_awaitMutex};
                            return !m_awaiting.empty() && !m_dbWaiting.load();
                        };
                        while (m_queue.projectWork() || m_indexing || documentsPending())
                            std::this_thread::sleep_for(std::chrono::milliseconds{20});
                        return lsp::Workspace_ExecuteCommandResult{nullptr};
                    });
                if (params.command != "svlsp.refresh")
                    throw lsp::RequestError(lsp::MessageError::InvalidParams,
                                            "unknown command '" + params.command + "'");
                bool rehash = false;
                if (params.arguments)
                    for (const auto& a : *params.arguments)
                        if (a.isString() && a.string() == "--rehash") rehash = true;
                refreshAll(rehash);
                std::promise<lsp::Workspace_ExecuteCommandResult> done;
                done.set_value(nullptr);
                return done.get_future();
            })
        .add<lsp::requests::Shutdown>(
            [this]() {
                return m_state.handleShutdown();
            })
        .add<lsp::notifications::Exit>(
            [this]() {
                m_state.handleExit();
            })
        .add<lsp::notifications::TextDocument_DidOpen>(
            [this](lsp::notifications::TextDocument_DidOpen::Params&& params) {
                const auto uri = params.textDocument.uri;
                const std::string path{uri.path()};
                {
                    std::lock_guard lock{m_dataMutex};
                    m_store.open(std::move(params));
                    // Discovers the file's project; a new one is queued to
                    // load first (plan.md §6.34 S4).
                    try {
                        m_projects.configFor(path);
                    } catch (const std::exception& e) {
                        std::cerr << "svlsp: " << e.what() << '\n';
                    }
                }
                awaitCompile(path);
                enqueueCompile(path, CompileQueue::kOpenDocument);
            })
        .add<lsp::notifications::TextDocument_DidChange>(
            [this](lsp::notifications::TextDocument_DidChange::Params&& params) {
                const auto uri = params.textDocument.uri;
                {
                    std::lock_guard lock{m_dataMutex};
                    m_store.update(std::move(params));
                }
                // Debounced: coalesces a burst of edits into one compile,
                // queued once typing pauses -- see plan.md §6.8.
                m_debouncer.schedule(uri.toString());
            })
        .add<lsp::notifications::TextDocument_DidSave>(
            [this](lsp::notifications::TextDocument_DidSave::Params&& params) {
                const auto uri = params.textDocument.uri;
                // Cancel any pending debounced compile for this URI first, so
                // a stale, already-superseded timer can't fire a redundant
                // publish right after this one — see plan.md §6.18.
                m_debouncer.cancel(uri.toString());
                awaitCompile(std::string{uri.path()});
                enqueueCompile(std::string{uri.path()}, CompileQueue::kOpenDocument);

                // Cross-file invalidation (plan.md §6.4): every file whose
                // own compiled unit `` `include ``s the just-saved file needs
                // recompiling too, since its content just changed under
                // them. Queued after the saved file, forced (their own text
                // may be unchanged), and published even if never opened.
                std::vector<std::string> includers;
                {
                    std::lock_guard lock{m_dataMutex};
                    includers = m_symbolDb.includersOf(std::string{uri.path()});
                }
                for (const auto& includerPath : includers)
                    enqueueCompile(includerPath, CompileQueue::kRest, /*force=*/true,
                                   /*openOnly=*/false);
            })
        .add<lsp::notifications::TextDocument_DidClose>(
            [this](lsp::notifications::TextDocument_DidClose::Params&& params) {
                const auto uri = params.textDocument.uri;
                m_debouncer.cancel(uri.toString());
                compiled(std::string{uri.path()});
                std::lock_guard lock{m_dataMutex};
                m_store.close(std::move(params));
            })
        .add<lsp::requests::TextDocument_Hover>(
            [this](lsp::HoverParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Hover>(uri, [this, params]() {
                    Request req{*this, "textDocument/hover",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_HoverResult{nullptr};
                    return HoverProvider::getHover(params, req.db(), doc->text);
                });
            })
        .add<lsp::requests::TextDocument_Definition>(
            [this](lsp::DefinitionParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Definition>(uri, [this, params]() {
                    Request req{*this, "textDocument/definition",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_DefinitionResult{nullptr};
                    return DefinitionProvider::getDefinition(params, req.db(), doc->text);
                });
            })
        .add<lsp::requests::TextDocument_References>(
            [this](lsp::ReferenceParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_References>(uri, [this, params]() {
                    Request req{*this, "textDocument/references",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_ReferencesResult{nullptr};
                    return ReferencesProvider::getReferences(
                        params, req.db(), doc->text,
                        [this](const std::string& path) { return currentTextFor(path); });
                });
            })
        .add<lsp::requests::TextDocument_Completion>(
            [this](lsp::CompletionParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Completion>(uri, [this, params]() {
                    Request req{*this, "textDocument/completion",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_CompletionResult{nullptr};
                    auto result = CompletionProvider::getCompletion(
                        params, req.db(), doc->text, m_state.fuzzyCompletionEnabled());
                    // While indexing, the list is incomplete: the client asks
                    // again as the user types (plan.md §6.34 S4).
                    if (!m_indexing) return result;
                    lsp::CompletionList list{.isIncomplete = true, .items = {}};
                    if (!result.isNull()) {
                        if (result.holdsAlternative<lsp::Array<lsp::CompletionItem>>())
                            list.items = std::move(result.get<lsp::Array<lsp::CompletionItem>>());
                        else
                            list.items = std::move(result.get<lsp::CompletionList>().items);
                    }
                    return lsp::TextDocument_CompletionResult{std::move(list)};
                });
            })
        .add<lsp::requests::CompletionItem_Resolve>(
            [this](lsp::CompletionItem&& item) {
                Request req{*this, "completionItem/resolve"};
                return CompletionProvider::resolve(std::move(item), req.db());
            })
        .add<lsp::requests::TextDocument_DocumentSymbol>(
            [this](lsp::DocumentSymbolParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_DocumentSymbol>(uri, [this, params]() {
                    Request req{*this, "textDocument/documentSymbol"};
                    return DocumentSymbolsProvider::getDocumentSymbols(params, req.db());
                });
            })
        .add<lsp::requests::Workspace_Symbol>(
            [this](lsp::WorkspaceSymbolParams&& params) {
                Request req{*this, "workspace/symbol"};
                return WorkspaceSymbolsProvider::getWorkspaceSymbols(params, req.db());
            })
        .add<lsp::requests::TextDocument_Rename>(
            [this](lsp::RenameParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Rename>(uri, [this, params]() {
                    Request req{*this, "textDocument/rename",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_RenameResult{nullptr};
                    return RenameProvider::getRename(
                        params, req.db(), doc->text,
                        [this](const std::string& path) { return currentTextFor(path); });
                });
            })
        .add<lsp::requests::TextDocument_SignatureHelp>(
            [this](lsp::SignatureHelpParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_SignatureHelp>(uri, [this, params]() {
                    Request req{*this, "textDocument/signatureHelp",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_SignatureHelpResult{nullptr};
                    return SignatureHelpProvider::getSignatureHelp(params, req.db(), doc->text);
                });
            });
}
