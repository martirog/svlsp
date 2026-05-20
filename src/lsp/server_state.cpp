#include "server_state.h"

auto ServerState::handleInitialize(lsp::InitializeParams params)
    -> lsp::requests::Initialize::Result
{
    if (m_phase.load() != Phase::Uninitialized)
        throw lsp::RequestError(lsp::MessageError::InvalidRequest,
                                "Server is already initialized");

    m_parentProcessId = params.processId;
    m_phase.store(Phase::Active);

    return {
        .capabilities = {
            .positionEncoding = lsp::PositionEncodingKind::UTF16,
            .textDocumentSync = lsp::TextDocumentSyncOptions{
                .openClose = true,
                .change    = lsp::TextDocumentSyncKind::Full,
                .save      = true,
            },
            .hoverProvider = lsp::OneOf<bool, lsp::HoverOptions>(true),
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
