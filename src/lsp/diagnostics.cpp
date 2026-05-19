#include "diagnostics.h"

DiagnosticsPublisher::DiagnosticsPublisher(lsp::MessageHandler& handler)
    : m_handler{handler}
{}

void DiagnosticsPublisher::publish(const lsp::DocumentUri& uri, int version,
                                   lsp::Array<lsp::Diagnostic> diags)
{
    m_handler.sendNotification<lsp::notifications::TextDocument_PublishDiagnostics>(
        buildParams(uri, version, std::move(diags))
    );
}

lsp::PublishDiagnosticsParams DiagnosticsPublisher::buildParams(
    const lsp::DocumentUri& uri, int version,
    lsp::Array<lsp::Diagnostic> diags)
{
    return {
        .uri         = uri,
        .diagnostics = std::move(diags),
        .version     = version,
    };
}
