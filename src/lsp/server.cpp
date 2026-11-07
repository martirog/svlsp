#include "server.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"
#include "lsp/library_db_builder.h"
#include "lsp/load_order.h"
#include "db/project_compiler.h"
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
{
    m_db.initSchema();
    m_compiler.setCalleeResolver(resolveCallees);
    m_compiler.setTypeChecker(checkTypeRefs);
    // A newly discovered project loads in the background (plan.md §6.34 S4).
    m_projects.deferLoading([this](const std::string& configPath) {
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
    m_awaitCv.wait(l, [&] { return !m_awaiting.count(path); });
}

bool LanguageServer::awaiting(const lsp::DocumentUri& uri)
{
    std::lock_guard l{m_awaitMutex};
    return m_awaiting.count(std::string{uri.path()}) > 0;
}

void LanguageServer::runJob(const CompileQueue::Job& job)
{
    switch (job.kind) {
    case CompileQueue::Job::Kind::Unit:          compileUnit(job); break;
    case CompileQueue::Job::Kind::LoadProject:   loadProject(job.key); break;
    case CompileQueue::Job::Kind::FinishProject: finishProject(job.key); break;
    case CompileQueue::Job::Kind::Refresh:       break;
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

void LanguageServer::loadProject(const std::string& configPath)
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
    try {
        LibraryDbBuilder::resolveLibraryDbSources(config, nullptr, "", m_compiler.walker());
    } catch (const std::exception& e) {
        std::cerr << "svlsp: " << e.what() << '\n';
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
        ProjectLoad load;
        load.plan = ProjectRefresher::plan(key, *c, m_compiler, m_symbolDb);
        load.progress = ProjectRefresher::begin(load.plan, m_symbolDb);
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
                m_projects.setExplicitConfigPath(m_state.explicitProjectConfigPath());
                m_compiler.setCollectDocs(m_state.docCommentsEnabled());
                return result;
            })
        .add<lsp::notifications::Initialized>(
            [this](lsp::notifications::Initialized::Params&&) {
                m_state.handleInitialized();
            })
        .add<lsp::requests::Workspace_ExecuteCommand>(
            [this](lsp::ExecuteCommandParams&& params)
                -> lsp::AsyncRequestResult<lsp::requests::Workspace_ExecuteCommand> {
                if (params.command == "svlsp.waitForIndexing")
                    // Off the message thread: requests keep being answered.
                    return std::async(std::launch::async, [this] {
                        while (m_queue.projectWork() || m_indexing)
                            std::this_thread::sleep_for(std::chrono::milliseconds{20});
                        return lsp::Workspace_ExecuteCommandResult{nullptr};
                    });
                throw lsp::RequestError(lsp::MessageError::InvalidParams,
                                        "unknown command '" + params.command + "'");
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
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/hover",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_HoverResult{nullptr};
                    return HoverProvider::getHover(params, m_symbolDb, doc->text);
                });
            })
        .add<lsp::requests::TextDocument_Definition>(
            [this](lsp::DefinitionParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Definition>(uri, [this, params]() {
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/definition",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_DefinitionResult{nullptr};
                    return DefinitionProvider::getDefinition(params, m_symbolDb, doc->text);
                });
            })
        .add<lsp::requests::TextDocument_References>(
            [this](lsp::ReferenceParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_References>(uri, [this, params]() {
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/references",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_ReferencesResult{nullptr};
                    return ReferencesProvider::getReferences(
                        params, m_symbolDb, doc->text,
                        [this](const std::string& path) { return currentTextFor(path); });
                });
            })
        .add<lsp::requests::TextDocument_Completion>(
            [this](lsp::CompletionParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Completion>(uri, [this, params]() {
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/completion",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_CompletionResult{nullptr};
                    auto result = CompletionProvider::getCompletion(
                        params, m_symbolDb, doc->text, m_state.fuzzyCompletionEnabled());
                    // While indexing, the list is incomplete: the client asks
                    // again as the user types (plan.md §6.34 S4).
                    if (!indexing()) return result;
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
                TimedLock lock{m_dataMutex, m_requestLog, "completionItem/resolve"};
                return CompletionProvider::resolve(std::move(item), m_symbolDb);
            })
        .add<lsp::requests::TextDocument_DocumentSymbol>(
            [this](lsp::DocumentSymbolParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_DocumentSymbol>(uri, [this, params]() {
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/documentSymbol"};
                    return DocumentSymbolsProvider::getDocumentSymbols(params, m_symbolDb);
                });
            })
        .add<lsp::requests::Workspace_Symbol>(
            [this](lsp::WorkspaceSymbolParams&& params) {
                TimedLock lock{m_dataMutex, m_requestLog, "workspace/symbol"};
                return WorkspaceSymbolsProvider::getWorkspaceSymbols(params, m_symbolDb);
            })
        .add<lsp::requests::TextDocument_Rename>(
            [this](lsp::RenameParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_Rename>(uri, [this, params]() {
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/rename",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_RenameResult{nullptr};
                    return RenameProvider::getRename(
                        params, m_symbolDb, doc->text,
                        [this](const std::string& path) { return currentTextFor(path); });
                });
            })
        .add<lsp::requests::TextDocument_SignatureHelp>(
            [this](lsp::SignatureHelpParams&& params) {
                const auto uri = params.textDocument.uri;
                return answer<lsp::requests::TextDocument_SignatureHelp>(uri, [this, params]() {
                    TimedLock lock{m_dataMutex, m_requestLog, "textDocument/signatureHelp",
                                requestWhere(params.textDocument.uri, params.position)};
                    const auto doc = m_store.find(params.textDocument.uri);
                    if (!doc) return lsp::TextDocument_SignatureHelpResult{nullptr};
                    return SignatureHelpProvider::getSignatureHelp(params, m_symbolDb, doc->text);
                });
            });
}
