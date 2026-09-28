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
    m_fuzzyCompletionEnabled = extractBoolOption(params, "fuzzyCompletion");
    m_docCommentsEnabled     = extractBoolOption(params, "docComments");
    m_phase.store(Phase::Active);

    return {
        .capabilities = {
            .positionEncoding = lsp::PositionEncodingKind::UTF16,
            .textDocumentSync = lsp::TextDocumentSyncOptions{
                .openClose = true,
                .change    = lsp::TextDocumentSyncKind::Full,
                .save      = true,
            },
            // Docs are fetched per item on resolve (plan.md §6.31), never
            // for the whole list.
            .completionProvider  = lsp::CompletionOptions{.resolveProvider = true},
            .hoverProvider            = lsp::OneOf<bool, lsp::HoverOptions>(true),
            // Every signature shape opens with '(' and advances on ','; a
            // `for (...)` header advances on ';', which only needs to
            // re-trigger help that is already showing.
            .signatureHelpProvider    = lsp::SignatureHelpOptions{
                .triggerCharacters   = lsp::Array<lsp::String>{"(", ","},
                .retriggerCharacters = lsp::Array<lsp::String>{";"},
            },
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

bool ServerState::extractBoolOption(const lsp::InitializeParams& params, std::string_view key)
{
    if (!params.initializationOptions) return true;
    const lsp::json::Value& opts = *params.initializationOptions;
    if (!opts.isObject()) return true;

    const lsp::json::Value* svlsp = opts.object().find("svlsp");
    if (!svlsp || !svlsp->isObject()) return true;

    const lsp::json::Value* value = svlsp->object().find(key);
    if (!value || !value->isBoolean()) return true;

    return value->boolean();
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
