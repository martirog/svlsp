#include "server_state.h"
#include <lsp/json/json.h>

auto ServerState::handleInitialize(lsp::InitializeParams params)
    -> lsp::requests::Initialize::Result
{
    if (m_phase.load() != Phase::Uninitialized)
        throw lsp::RequestError(lsp::MessageError::InvalidRequest,
                                "Server is already initialized");

    m_parentProcessId = params.processId;
    m_rootUri = params.rootUri;
    m_explicitProjectConfigPath = extractProjectConfigPath(params);
    m_phase.store(Phase::Active);

    return {
        .capabilities = {
            .positionEncoding = lsp::PositionEncodingKind::UTF16,
            .textDocumentSync = lsp::TextDocumentSyncOptions{
                .openClose = true,
                .change    = lsp::TextDocumentSyncKind::Full,
                .save      = true,
            },
            .completionProvider  = lsp::CompletionOptions{},
            .hoverProvider            = lsp::OneOf<bool, lsp::HoverOptions>(true),
            .signatureHelpProvider    = lsp::SignatureHelpOptions{},
            .definitionProvider       = lsp::OneOf<bool, lsp::DefinitionOptions>(true),
            .referencesProvider      = lsp::OneOf<bool, lsp::ReferenceOptions>(true),
            .documentSymbolProvider  = lsp::OneOf<bool, lsp::DocumentSymbolOptions>(true),
            .workspaceSymbolProvider = lsp::OneOf<bool, lsp::WorkspaceSymbolOptions>(true),
            .renameProvider          = lsp::OneOf<bool, lsp::RenameOptions>(true),
        },
        .serverInfo = lsp::InitializeResultServerInfo{
            .name    = "svlsp",
            .version = "0.1.0",
        },
    };
}

void ServerState::handleInitialized()
{
    // The 'initialized' notification confirms the client received our
    // InitializeResult. Nothing to do yet; reserved for future cache warm-up.
}

auto ServerState::handleShutdown() -> lsp::requests::Shutdown::Result
{
    requireActive("shutdown");
    m_phase.store(Phase::Shutdown);
    return {};
}

void ServerState::handleExit()
{
    // Per LSP spec: exit after shutdown is a clean exit (code 0).
    // Exit without prior shutdown is an error exit (code 1), but we treat
    // both the same here — the binary's exit code is set by LanguageServer.
    m_phase.store(Phase::Inactive);
}

std::string ServerState::extractProjectConfigPath(const lsp::InitializeParams& params)
{
    if (!params.initializationOptions) return "";
    const lsp::json::Value& opts = *params.initializationOptions;
    if (!opts.isObject()) return "";

    const lsp::json::Value* svlsp = opts.object().find("svlsp");
    if (!svlsp || !svlsp->isObject()) return "";

    const lsp::json::Value* projectConfig = svlsp->object().find("projectConfig");
    if (!projectConfig || !projectConfig->isString()) return "";

    return projectConfig->string();
}

void ServerState::requireActive(const char* method) const
{
    const auto phase = m_phase.load();

    if (phase == Phase::Uninitialized)
        throw lsp::RequestError(lsp::MessageError::ServerNotInitialized,
                                std::string(method) + ": server not initialized");

    if (phase == Phase::Shutdown || phase == Phase::Inactive)
        throw lsp::RequestError(lsp::MessageError::InvalidRequest,
                                std::string(method) + ": server is shut down");
}
