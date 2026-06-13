#include "server.h"
#include <iostream>
#include <lsp/messages.h>
#include <lsp/process.h>

LanguageServer::LanguageServer(lsp::io::Stream& io)
    : m_db{":memory:"}
    , m_symbolDb{m_db}
    , m_compiler{m_symbolDb}
    , m_connection{io}
    , m_messageHandler{m_connection}
    , m_diagnostics{m_messageHandler}
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
    auto parseErrors = m_compiler.compile(path, text);

    lsp::Array<lsp::Diagnostic> diags;
    for (const auto& err : parseErrors)
        diags.push_back(DiagnosticsPublisher::buildDiagnostic(err));
    return diags;
}

void LanguageServer::registerHandlers()
{
    m_messageHandler
        .add<lsp::requests::Initialize>(
            [this](lsp::InitializeParams&& params) {
                return m_state.handleInitialize(std::move(params));
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
                const auto uri     = params.textDocument.uri;
                const auto version = params.textDocument.version;
                m_store.open(std::move(params));
                m_diagnostics.publish(uri, version,
                    parseDiagnostics(uri, m_store.get(uri).text));
            })
        .add<lsp::notifications::TextDocument_DidChange>(
            [this](lsp::notifications::TextDocument_DidChange::Params&& params) {
                const auto uri     = params.textDocument.uri;
                const auto version = params.textDocument.version;
                m_store.update(std::move(params));
                m_diagnostics.publish(uri, version,
                    parseDiagnostics(uri, m_store.get(uri).text));
            })
        .add<lsp::notifications::TextDocument_DidClose>(
            [this](lsp::notifications::TextDocument_DidClose::Params&& params) {
                m_store.close(std::move(params));
            })
        .add<lsp::requests::TextDocument_Hover>(
            [this](lsp::HoverParams&& params) {
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_HoverResult{nullptr};
                return HoverProvider::getHover(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text);
            })
        .add<lsp::requests::TextDocument_Definition>(
            [this](lsp::DefinitionParams&& params) {
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
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_CompletionResult{nullptr};
                return CompletionProvider::getCompletion(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text);
            })
        .add<lsp::requests::TextDocument_DocumentSymbol>(
            [this](lsp::DocumentSymbolParams&& params) {
                return DocumentSymbolsProvider::getDocumentSymbols(params, m_symbolDb);
            })
        .add<lsp::requests::Workspace_Symbol>(
            [this](lsp::WorkspaceSymbolParams&& params) {
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
