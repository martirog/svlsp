#pragma once

#include <lsp/messages.h>
#include <lsp/messagehandler.h>

// DiagnosticsPublisher sends textDocument/publishDiagnostics notifications
// to the client.  At this stage (pre-ANTLR4) all published diagnostic sets
// are empty — the publisher establishes the server→client push mechanism
// that Phase 4 will populate with real parse errors.
class DiagnosticsPublisher {
public:
    explicit DiagnosticsPublisher(lsp::MessageHandler& handler);

    // Send publishDiagnostics for the given URI.  Pass an empty vector (the
    // default) to clear any previously published diagnostics for that URI.
    void publish(const lsp::DocumentUri& uri, int version,
                 lsp::Array<lsp::Diagnostic> diags = {});

    // Build the params without sending — used by unit tests.
    static lsp::PublishDiagnosticsParams buildParams(
        const lsp::DocumentUri& uri, int version,
        lsp::Array<lsp::Diagnostic> diags = {});

private:
    lsp::MessageHandler& m_handler;
};
