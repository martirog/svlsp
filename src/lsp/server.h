#pragma once

#include <iosfwd>
#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/io/standardio.h>
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
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"

// LanguageServer wires the lsp-framework transport and dispatch layer to the
// ServerState business logic.  All I/O happens here; ServerState stays pure.
class LanguageServer {
public:
    // `logStream`, when non-null, is forwarded to CompilationController so
    // every file it parses/persists gets logged — see
    // CompilationController's own doc comment.
    explicit LanguageServer(lsp::io::Stream& io, std::ostream* logStream = nullptr);

    // Runs the message loop until the client sends 'exit'. Returns the exit
    // code the process should use (0 after clean shutdown, 1 otherwise).
    int run();

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

    void registerHandlers();

    // Run the compiler pipeline (or return cached DB result) and publish diagnostics.
    lsp::Array<lsp::Diagnostic> parseDiagnostics(const lsp::DocumentUri& uri,
                                                  const std::string& text);
};
