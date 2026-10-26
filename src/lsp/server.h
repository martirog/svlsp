#pragma once

#include <iosfwd>
#include <mutex>
#include <optional>
#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/io/standardio.h>
#include "change_debouncer.h"
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

    // Guards every access to m_store, m_db, m_symbolDb, m_compiler, and
    // m_projects. Needed because compileAndPublish() can now run on
    // m_debouncer's background thread concurrently with the message-read
    // thread handling a request (hover/definition/completion/documentSymbol/
    // workspace-symbol) or another notification (didOpen/didClose) — see
    // plan.md §6.8. A single coarse mutex is the documented "minimum fix";
    // a compile holds it only to read and to write the DB, not across its
    // parse (compileCurrent).
    std::mutex m_dataMutex;
    // Runs compiles one at a time: one's prepare and commit must not
    // interleave with another's (CompilationController::prepare). Taken
    // before m_dataMutex, never while holding it.
    std::mutex m_compileMutex;
    std::ostream* m_requestLog; // `--log-requests`; null = off

    // "<path>:<line>:<character>" for a request's log line ("" with no log).
    std::string requestWhere(const lsp::DocumentUri& uri, const lsp::Position& pos) const;

    // Debounces textDocument/didChange: coalesces a burst of edits into one
    // compileAndPublish() call, fired on its own thread once typing pauses —
    // see plan.md §6.8 and change_debouncer.h. Declared last so it is
    // destroyed (and its worker thread joined) before any member it calls
    // back into.
    ChangeDebouncer m_debouncer;

    // Cross-file invalidation (plan.md §6.4): on didSave, every file whose
    // own compiled unit `` `include ``s the saved file (SymbolDatabase::
    // includersOf) is scheduled here instead of recompiled inline — its own
    // background thread drains them one at a time, each independently
    // mutex-scoped (see forceRecompileAndPublish), so a large fan-out from a
    // widely-`` `include ``d file never holds m_dataMutex for longer than one
    // file's compile time and never blocks the message-read thread. Reuses
    // ChangeDebouncer rather than a bespoke queue -- its per-key coalescing
    // is exactly right here too (a file already pending from the user's own
    // typing that also gets flagged as a dependent should still fire once,
    // not twice). Declared last for the same destruction-order reason as
    // m_debouncer above.
    ChangeDebouncer m_dependencyRechecker;

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
    std::optional<CompileOutcome> compileCurrent(const lsp::DocumentUri& uri, bool openOnly,
                                                 bool forceRecompile, const char* what);
    void publish(const lsp::DocumentUri& uri, CompileOutcome&& out);

    // Converts already-computed ParseErrors to LSP Diagnostics.
    static lsp::Array<lsp::Diagnostic> toDiagnostics(const std::vector<ParseError>& errs);

    // For each path, reads its current diagnostics back from the DB and
    // pairs them with its URI, ready to publish once the caller releases
    // m_dataMutex. Caller must hold m_dataMutex while calling this. Shared
    // by compileAndPublish and forceRecompileAndPublish.
    std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>>
    collectIncludedDiagnostics(const std::vector<std::string>& paths);

    // Returns `path`'s current text: m_store's live copy if open, else a
    // fresh disk read. std::nullopt if neither is available (e.g. a file
    // referenced by some stale DB row that was since deleted). Caller must
    // hold m_dataMutex. Shared by forceRecompileAndPublish and the
    // references/rename handlers, both of which need "current text for an
    // arbitrary file the DB knows about," not just the requesting document.
    std::optional<std::string> currentTextFor(const std::string& path);

    // Compiles the given (already-open) document's current text and
    // publishes its diagnostics, plus diagnostics for every `` `include ``d
    // file touched by this compile (read back from the DB — those files were
    // never `didOpen`ed, so they're published with no client-tracked
    // version). Locks m_dataMutex itself — do not call while already holding
    // it. If the document was closed in the meantime (e.g. a debounced fire
    // racing a didClose), this is a harmless no-op.
    void compileAndPublish(const lsp::DocumentUri& uri);

    // Cross-file invalidation (plan.md §6.4): force-recompiles `path`
    // (bypassing CompilationController's content-hash cache — `path`'s own
    // text may be unchanged, only something it `` `include ``s changed) and
    // publishes its diagnostics, plus every included file's own. Reads
    // current text from m_store if `path` is open (attaching its real
    // client-tracked version), else from disk (no version — never
    // `didOpen`ed). A disk read failure (e.g. the file was deleted since
    // being scheduled) is silently skipped. Locks m_dataMutex itself; called
    // from m_dependencyRechecker's own background thread, never inline from
    // a request/notification handler.
    void forceRecompileAndPublish(const std::string& path);
};
