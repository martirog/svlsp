#pragma once

#include <iosfwd>
#include <mutex>
#include <optional>
#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/io/standardio.h>
#include "change_debouncer.h"
#include "compile_queue.h"
#include "completion.h"
#include "definition.h"
#include "diagnostics.h"
#include "document_symbols.h"
#include "document_store.h"
#include "hover.h"
#include "references.h"
#include "rename.h"
#include "server_state.h"
#include "signature_help.h"
#include "workspace_symbols.h"
#include "project_registry.h"
#include "request_log.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "db/project_refresher.h"
#include "db/db_lock.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <map>
#include <set>
#include <unordered_set>

// LanguageServer wires the lsp-framework transport and dispatch layer to the
// ServerState business logic.  All I/O happens here; ServerState stays pure.
class LanguageServer {
public:
    // `logStream`, when non-null, is forwarded to CompilationController so
    // every file it parses/persists gets logged — see
    // CompilationController's own doc comment. `requestLog`, when non-null,
    // gets one timing line per request and compile (request_log.h).
    explicit LanguageServer(lsp::io::Stream& io, std::ostream* logStream = nullptr,
                            std::ostream* requestLog = nullptr);

    // Runs the message loop until the client sends 'exit'. Returns the exit
    // code the process should use (0 after clean shutdown, 1 otherwise).
    int run();

    // The svlsp build (SVLSP_GIT_VERSION) recorded in a project DB; a DB
    // recorded by another build is reset (plan.md §6.34 S3). Call before
    // run(); unset, no check.
    void setVersion(std::string version) { m_version = std::move(version); }

    // The parse every compile runs (CompilationController::setWalker), e.g.
    // a ParseWorker's. Call before run().
    void setWalker(CompilationController::Walker walker) { m_compiler.setWalker(std::move(walker)); }

private:
    ServerState           m_state;
    DocumentStore         m_store;
    Database              m_db;          // in-memory for now; file path in Phase 6
    SymbolDatabase        m_symbolDb;
    CompilationController m_compiler;
    ProjectRegistry        m_projects;    // must follow m_compiler/m_symbolDb
    lsp::Connection       m_connection;
    lsp::MessageHandler   m_messageHandler;
    DiagnosticsPublisher  m_diagnostics; // must follow m_messageHandler

    // Guards every access to m_store, m_db, m_symbolDb, m_compiler,
    // m_projects and m_loads: compiles run on m_queue's thread while the
    // message-read thread handles requests and notifications (plan.md §6.8,
    // §6.34 S4). A compile holds it only to read and to write the DB, not
    // across its parse (compileCurrent).
    std::mutex m_dataMutex;
    // Runs compiles one at a time: one's prepare and commit must not
    // interleave with another's (CompilationController::prepare). Taken
    // before m_dataMutex, never while holding it.
    std::mutex m_compileMutex;
    std::ostream* m_requestLog; // `--log-requests`; null = off

    // The project DB (plan.md §6.34 S3). Memory: ":memory:" (no DB set, the
    // default). Writer: the file, holding its lock. Waiting: a --build-db
    // holds the lock; the file is open read-only and the queue waits for
    // the lock before its next job. Copy: another server holds the lock;
    // the file was copied into memory and is never written (D2). Guarded
    // by m_dataMutex.
    enum class DbMode { Memory, Writer, Waiting, Copy };
    DbMode      m_dbMode{DbMode::Memory};
    std::string m_dbPath;
    DbLock      m_dbLock;
    std::string m_version;      // setVersion()
    std::string m_buildVersion; // m_version and the doc-comment setting

    // Requests read through a second, read-only connection when the DB is
    // a file this server writes (plan.md §6.34 S5): WAL gives it the last
    // committed state, so a request never waits for a compile's commit.
    // Only the message thread uses it (requests), under m_readMutex for the
    // request log; it syncs (attachments, caches) when m_commitGeneration
    // moved. m_readerAttached is what the writer attached, under
    // m_readSyncMutex.
    Database              m_readConn{":memory:"};
    SymbolDatabase        m_readDb{m_readConn};
    std::atomic<bool>     m_readerReady{false};
    std::atomic<uint64_t> m_commitGeneration{0};
    uint64_t              m_readGeneration{0};
    std::mutex            m_readMutex;
    std::mutex            m_readSyncMutex;
    std::vector<std::string> m_readerAttached;
    std::atomic<bool>     m_indexing{false}; // !m_loads.empty(), for requests
    void syncReader();
    std::vector<std::string> m_readerReattach; // rebuilt libraries to re-attach (S7)
    void noteAttached(const std::vector<std::string>& libraryDbs,
                      const std::vector<std::string>& reattach = {});
    // Library archives already reported stale (plan.md §6.34 S7): once each.
    std::set<std::string> m_staleWarned;

    // A request's access to the DB: m_dataMutex and the writer's
    // connection, or (reader ready) the reader in one read transaction --
    // all of a request's queries see the same commit.
    class Request {
    public:
        Request(LanguageServer& server, const char* what, std::string where = {});
        ~Request();
        SymbolDatabase& db() { return *m_db; }
        Request(const Request&)            = delete;
        Request& operator=(const Request&) = delete;

    private:
        bool            m_reader;
        TimedLock       m_lock;
        SymbolDatabase* m_db;
    };

    // Documents whose compile since didOpen / didSave hasn't run yet, by
    // path: a request on one waits for it (plan.md §6.34 S4) -- the
    // document's own answers need its own compile, not the project load's.
    // Not while the queue waits for a --build-db (m_dbWaiting).
    std::mutex              m_awaitMutex;
    std::condition_variable m_awaitCv;
    std::set<std::string>   m_awaiting;
    std::atomic<bool>       m_dbWaiting{false};
    void awaitCompile(const std::string& path);
    void compiled(const std::string& path);
    void waitForDocument(const lsp::DocumentUri& uri);
    bool awaiting(const lsp::DocumentUri& uri);

    // Answers request M with `body`: at once, or -- when `uri` still waits
    // for its compile -- from another thread once it has run, so the
    // message thread keeps handling messages (the client's answers to the
    // server's own requests among them).
    template <typename M, typename F>
    lsp::AsyncRequestResult<M> answer(const lsp::DocumentUri& uri, F body)
    {
        if (!awaiting(uri)) {
            std::promise<typename M::Result> done;
            try {
                done.set_value(body());
            } catch (...) {
                done.set_exception(std::current_exception());
            }
            return done.get_future();
        }
        return std::async(std::launch::async, [this, uri, body = std::move(body)]() {
            waitForDocument(uri);
            return body();
        });
    }

    // Makes `path` the server's DB, unless one was chosen already or
    // something was compiled into memory. Caller holds m_dataMutex.
    void openProjectDb(const std::string& path);
    // Opens m_dbPath read-write (the lock is held). Caller holds m_dataMutex.
    void becomeWriter();
    // Resets a DB recorded by another svlsp build. Caller holds m_dataMutex.
    void checkDbVersion();
    // Before a queue job: in Waiting mode, waits for the lock and becomes
    // the writer. Takes m_dataMutex itself.
    void waitForWriter();

    // "<path>:<line>:<character>" for a request's log line ("" with no log).
    std::string requestWhere(const lsp::DocumentUri& uri, const lsp::Position& pos) const;

    // A project being loaded or refreshed in the background (plan.md §6.34
    // S4): its refresh plan, the refresh's progress, and the units of the
    // plan not compiled yet. Guarded by m_dataMutex; touched by the queue's
    // thread, read by requests (indexing()).
    struct ProjectLoad {
        ProjectRefresher::Plan     plan;
        ProjectRefresher::Progress progress;
        std::unordered_set<std::string> pending;
        size_t total{0};
        std::string progressToken; // "" = no progress reported
    };
    std::map<std::string, ProjectLoad> m_loads; // by config path

    // Every path published so far (guarded by m_publishMutex). A load's
    // units publish only these and the open documents: a project load must
    // not flood the client with every file's diagnostics.
    std::mutex m_publishMutex;
    std::unordered_set<std::string> m_published;

    // `window/workDoneProgress/create` tokens the client acknowledged.
    std::mutex m_progressMutex;
    std::set<std::string> m_progressReady;
    std::map<std::string, std::string> m_progressEndedEarly; // token -> end message
    int m_nextProgressToken{0};

    // Every compile runs here (plan.md §6.34 S4): didOpen, debounced
    // didChange, didSave and its includers (plan.md §6.4), and a project's
    // load, unit by unit. Declared after everything a job uses, so it is
    // destroyed (its thread joined) first -- after m_debouncer, which pushes
    // into it.
    CompileQueue m_queue;

    // Debounces textDocument/didChange: coalesces a burst of edits into one
    // compile, queued from its own thread once typing pauses -- see plan.md
    // §6.8 and change_debouncer.h. Declared last so it is destroyed (and its
    // thread joined) before the queue it pushes into.
    ChangeDebouncer m_debouncer;

    // Coalesces the file events of a checkout (workspace/didChangeWatchedFiles,
    // plan.md §6.34 S6) into one refresh of every loaded project, once they
    // stop for 500 ms. Declared after m_queue, which it pushes into.
    ChangeDebouncer m_refreshDebouncer;

    void registerHandlers();

    // What a compile publishes: the document's diagnostics (with its
    // client-tracked version if open) and {URI, diagnostics} for every
    // `` `include ``d file it recompiled -- those were never `didOpen`ed, so
    // they're published with no version (empty on a cache hit, see
    // CompilationController::compile).
    struct CompileOutcome {
        std::optional<int> version;
        lsp::Array<lsp::Diagnostic> diags;
        std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>> includedDiags;
    };

    // Compiles `uri`'s current text: the open document's, else (unless
    // `openOnly`) the file on disk; std::nullopt when there is none. Takes
    // m_compileMutex, and m_dataMutex only for CompilationController's
    // prepare and commit, so requests and notifications are handled during
    // the parse. Logs one `what` line over the whole compile. Do not call
    // while holding either mutex.
    // `project`, when set, is the config path the unit compiles under (a
    // load's unit), instead of the one discovered for it. While a project
    // is indexing, the diagnostics that depend on other units are held back
    // (heldBack()).
    std::optional<CompileOutcome> compileCurrent(const lsp::DocumentUri& uri, bool openOnly,
                                                 bool forceRecompile, const char* what,
                                                 const std::string& project = "");
    void publish(const lsp::DocumentUri& uri, CompileOutcome&& out);

    // Converts already-computed ParseErrors to LSP Diagnostics.
    static lsp::Array<lsp::Diagnostic> toDiagnostics(const std::vector<ParseError>& errs);

    // For each path, reads its current diagnostics back from the DB and
    // pairs them with its URI, ready to publish once the caller releases
    // m_dataMutex. Caller must hold m_dataMutex while calling this. Shared
    // by compileAndPublish and forceRecompileAndPublish.
    std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>>
    collectIncludedDiagnostics(const std::vector<std::string>& paths, bool holdBack = false);

    // `path`'s diagnostics from the DB, without the ones that depend on
    // other units when `holdBack` (declared-type warnings, missing
    // arguments, unresolved instantiations). Caller holds m_dataMutex.
    lsp::Array<lsp::Diagnostic> storedDiagnostics(const std::string& path, bool holdBack);

    // Whether a project is loading or refreshing. Caller holds m_dataMutex.
    bool indexing() const { return !m_loads.empty(); }

    // The queue's jobs.
    void runJob(const CompileQueue::Job& job);
    // A project's load, and every refresh after it (plan.md §6.34 S4, S6):
    // a refresh rereads the config first; `rehash` reads every file.
    void loadProject(const std::string& configPath, bool rehash = false);
    void refreshProject(const std::string& configPath, bool rehash);
    // Queues a refresh of every loaded project.
    void refreshAll(bool rehash);
    void compileUnit(const CompileQueue::Job& job);
    void finishProject(const std::string& configPath);

    // Work-done progress for a load (plan.md §6.34 S4); no-ops when the
    // client doesn't support it.
    std::string beginProgress(const std::string& title, const std::string& message);
    void reportProgress(const std::string& token, const std::string& message, unsigned percent);
    void endProgress(const std::string& token, const std::string& message);

    // Returns `path`'s current text: m_store's live copy if open, else a
    // fresh disk read. std::nullopt if neither is available (e.g. a file
    // referenced by some stale DB row that was since deleted). Caller must
    // hold m_dataMutex. Shared by forceRecompileAndPublish and the
    // references/rename handlers, both of which need "current text for an
    // arbitrary file the DB knows about," not just the requesting document.
    std::optional<std::string> currentTextFor(const std::string& path);

    // Queues a compile of `path` (a Unit job) at `rank`.
    void enqueueCompile(const std::string& path, int rank, bool force = false,
                        bool openOnly = true, bool publishAll = true);

};
