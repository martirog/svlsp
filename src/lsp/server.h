#pragma once

#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/io/standardio.h>
#include "server_state.h"

// LanguageServer wires the lsp-framework transport and dispatch layer to the
// ServerState business logic.  All I/O happens here; ServerState stays pure.
class LanguageServer {
public:
    explicit LanguageServer(lsp::io::Stream& io);

    // Runs the message loop until the client sends 'exit'. Returns the exit
    // code the process should use (0 after clean shutdown, 1 otherwise).
    int run();

private:
    ServerState         m_state;
    lsp::Connection     m_connection;
    lsp::MessageHandler m_messageHandler;

    void registerHandlers();
};
