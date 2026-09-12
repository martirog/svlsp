#pragma once

#include <lsp/messages.h>
#include <lsp/messagehandler.h>
#include <optional>
#include "compiler/parse_record.h"

// DiagnosticsPublisher sends textDocument/publishDiagnostics notifications
// to the client.  At this stage (pre-ANTLR4) all published diagnostic sets
// are empty — the publisher establishes the server→client push mechanism
// that Phase 4 will populate with real parse errors.
class DiagnosticsPublisher {
public:
    explicit DiagnosticsPublisher(lsp::MessageHandler& handler);

    // Send publishDiagnostics for the given URI.  Pass an empty vector (the
    // default) to clear any previously published diagnostics for that URI.
    // `version` is the document version the client is tracking; pass
    // std::nullopt for a file the client never `didOpen`ed (e.g. an
    // `` `include ``d file whose diagnostics are published on the primary
    // file's behalf) — the LSP spec's own `version` field is optional for
    // exactly this case.
    void publish(const lsp::DocumentUri& uri, std::optional<int> version,
                 lsp::Array<lsp::Diagnostic> diags = {});

    // Build the params without sending — used by unit tests.
    static lsp::PublishDiagnosticsParams buildParams(
        const lsp::DocumentUri& uri, std::optional<int> version,
        lsp::Array<lsp::Diagnostic> diags = {});

    // Convert a compiler ParseError to an LSP Diagnostic.
    // ANTLR4 lines are 1-based; LSP positions are 0-based.
    static lsp::Diagnostic buildDiagnostic(const ParseError& err);

private:
    lsp::MessageHandler& m_handler;
};
