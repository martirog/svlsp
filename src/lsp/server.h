#pragma once

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
#include "server_state.h"
#include "workspace_symbols.h"

// LanguageServer wires the lsp-framework transport and dispatch layer to the
// ServerState business logic.  All I/O happens here; ServerState stays pure.
class LanguageServer {
public:
    explicit LanguageServer(lsp::io::Stream& io);

    // Runs the message loop until the client sends 'exit'. Returns the exit
    // code the process should use (0 after clean shutdown, 1 otherwise).
    int run();

private:
    ServerState          m_state;
    DocumentStore        m_store;
    lsp::Connection      m_connection;
    lsp::MessageHandler  m_messageHandler;
    DiagnosticsPublisher m_diagnostics;  // must follow m_messageHandler

    void registerHandlers();
};
