#include "diagnostics.h"
#include "lsp/symbol_utils.h"
#include <lsp/types.h>

DiagnosticsPublisher::DiagnosticsPublisher(lsp::MessageHandler& handler)
    : m_handler{handler}
{}

void DiagnosticsPublisher::publish(const lsp::DocumentUri& uri, std::optional<int> version,
                                   lsp::Array<lsp::Diagnostic> diags)
{
    m_handler.sendNotification<lsp::notifications::TextDocument_PublishDiagnostics>(
        buildParams(uri, version, std::move(diags))
    );
}

lsp::PublishDiagnosticsParams DiagnosticsPublisher::buildParams(
    const lsp::DocumentUri& uri, std::optional<int> version,
    lsp::Array<lsp::Diagnostic> diags)
{
    lsp::PublishDiagnosticsParams params;
    // The client's name for the file (plan.md §6.40 D6).
    params.uri = uri.scheme() == "file" ? clientUri(std::string{uri.path()}) : uri;
    params.diagnostics = std::move(diags);
    if (version) params.version = *version;
    return params;
}

lsp::Diagnostic DiagnosticsPublisher::buildDiagnostic(const ParseError& err)
{
    // ANTLR4 lines are 1-based; LSP positions are 0-based.
    lsp::uint lspLine = static_cast<lsp::uint>(err.line - 1);
    lsp::uint lspCol  = static_cast<lsp::uint>(err.column);

    lsp::Diagnostic diag;
    diag.range = {
        .start = {.line = lspLine, .character = lspCol},
        .end   = {.line = lspLine,
                  .character = err.endColumn > err.column
                                   ? static_cast<lsp::uint>(err.endColumn)
                                   : lspCol + 1},
    };
    diag.message  = err.message;
    diag.severity = lsp::DiagnosticSeverityEnum{
        err.severity == 3   ? lsp::DiagnosticSeverity::Information
        : err.severity == 2 ? lsp::DiagnosticSeverity::Warning
                            : lsp::DiagnosticSeverity::Error};
    return diag;
}
