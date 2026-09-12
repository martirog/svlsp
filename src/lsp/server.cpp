#include "server.h"
#include <iostream>
#include <lsp/messages.h>
#include <lsp/process.h>
#include <lsp/uri.h>

LanguageServer::LanguageServer(lsp::io::Stream& io, std::ostream* logStream)
    : m_db{":memory:"}
    , m_symbolDb{m_db}
    , m_compiler{m_symbolDb, logStream}
    , m_projects{m_compiler, m_symbolDb}
    , m_connection{io}
    , m_messageHandler{m_connection}
    , m_diagnostics{m_messageHandler}
    , m_debouncer{[this](const std::string& uriStr) {
          compileAndPublish(lsp::DocumentUri{lsp::Uri::parse(uriStr)});
      }}
{
    m_db.initSchema();
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

lsp::Array<lsp::Diagnostic> LanguageServer::parseDiagnostics(
    const lsp::DocumentUri& uri, const std::string& text)
{
    const std::string path{uri.path()};
    auto parseErrors = m_compiler.compile(path, text, m_projects.configFor(path));

    lsp::Array<lsp::Diagnostic> diags;
    for (const auto& err : parseErrors)
        diags.push_back(DiagnosticsPublisher::buildDiagnostic(err));
    return diags;
}

void LanguageServer::compileAndPublish(const lsp::DocumentUri& uri)
{
    lsp::Array<lsp::Diagnostic> diags;
    int version = 0;
    {
        std::lock_guard lock{m_dataMutex};
        if (!m_store.contains(uri))
            return; // closed before this fired (didClose cancels the pending
                     // debounce entry, but guard here too for the narrow race)
        version = m_store.get(uri).version;
        diags   = parseDiagnostics(uri, m_store.get(uri).text);
    }
    m_diagnostics.publish(uri, version, diags);
}

void LanguageServer::registerHandlers()
{
    m_messageHandler
        .add<lsp::requests::Initialize>(
            [this](lsp::InitializeParams&& params) {
                auto result = m_state.handleInitialize(std::move(params));
                m_projects.setExplicitConfigPath(m_state.explicitProjectConfigPath());
                return result;
            })
        .add<lsp::notifications::Initialized>(
            [this](lsp::notifications::Initialized::Params&&) {
                m_state.handleInitialized();
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
                {
                    std::lock_guard lock{m_dataMutex};
                    m_store.open(std::move(params));
                }
                compileAndPublish(uri);
            })
        .add<lsp::notifications::TextDocument_DidChange>(
            [this](lsp::notifications::TextDocument_DidChange::Params&& params) {
                const auto uri = params.textDocument.uri;
                {
                    std::lock_guard lock{m_dataMutex};
                    m_store.update(std::move(params));
                }
                // Debounced: coalesces a burst of edits into one
                // compileAndPublish(), fired off the message-read thread
                // once typing pauses — see plan.md §6.8.
                m_debouncer.schedule(uri.toString());
            })
        .add<lsp::notifications::TextDocument_DidSave>(
            [this](lsp::notifications::TextDocument_DidSave::Params&& params) {
                const auto uri = params.textDocument.uri;
                // Cancel any pending debounced compile for this URI first, so
                // a stale, already-superseded timer can't fire a redundant
                // publish right after this one — see plan.md §6.18.
                m_debouncer.cancel(uri.toString());
                compileAndPublish(uri);
            })
        .add<lsp::notifications::TextDocument_DidClose>(
            [this](lsp::notifications::TextDocument_DidClose::Params&& params) {
                const auto uri = params.textDocument.uri;
                m_debouncer.cancel(uri.toString());
                std::lock_guard lock{m_dataMutex};
                m_store.close(std::move(params));
            })
        .add<lsp::requests::TextDocument_Hover>(
            [this](lsp::HoverParams&& params) {
                std::lock_guard lock{m_dataMutex};
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_HoverResult{nullptr};
                return HoverProvider::getHover(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text);
            })
        .add<lsp::requests::TextDocument_Definition>(
            [this](lsp::DefinitionParams&& params) {
                std::lock_guard lock{m_dataMutex};
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_DefinitionResult{nullptr};
                return DefinitionProvider::getDefinition(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text);
            })
        .add<lsp::requests::TextDocument_References>(
            [](lsp::ReferenceParams&& params) {
                return ReferencesProvider::getReferences(params);
            })
        .add<lsp::requests::TextDocument_Completion>(
            [this](lsp::CompletionParams&& params) {
                std::lock_guard lock{m_dataMutex};
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_CompletionResult{nullptr};
                return CompletionProvider::getCompletion(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text,
                    m_state.fuzzyCompletionEnabled());
            })
        .add<lsp::requests::TextDocument_DocumentSymbol>(
            [this](lsp::DocumentSymbolParams&& params) {
                std::lock_guard lock{m_dataMutex};
                return DocumentSymbolsProvider::getDocumentSymbols(params, m_symbolDb);
            })
        .add<lsp::requests::Workspace_Symbol>(
            [this](lsp::WorkspaceSymbolParams&& params) {
                std::lock_guard lock{m_dataMutex};
                return WorkspaceSymbolsProvider::getWorkspaceSymbols(params, m_symbolDb);
            })
        .add<lsp::requests::TextDocument_Rename>(
            [](lsp::RenameParams&& params) {
                return RenameProvider::getRename(params);
            })
        .add<lsp::requests::TextDocument_SignatureHelp>(
            [](lsp::SignatureHelpParams&& params) {
                return SignatureHelpProvider::getSignatureHelp(params);
            });
}
