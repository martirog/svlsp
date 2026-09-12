#include "server.h"
#include "lsp/symbol_utils.h"
#include "compiler/file_utils.h"
#include <iostream>
#include <optional>
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
    , m_dependencyRechecker{[this](const std::string& path) {
          forceRecompileAndPublish(path);
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
    const lsp::DocumentUri& uri, const std::string& text,
    std::vector<std::string>* includedFiles)
{
    const std::string path{uri.path()};
    auto parseErrors = m_compiler.compile(path, text, m_projects.configFor(path), includedFiles);
    return toDiagnostics(parseErrors);
}

lsp::Array<lsp::Diagnostic> LanguageServer::toDiagnostics(const std::vector<ParseError>& errs)
{
    lsp::Array<lsp::Diagnostic> diags;
    for (const auto& err : errs)
        diags.push_back(DiagnosticsPublisher::buildDiagnostic(err));
    return diags;
}

std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>>
LanguageServer::collectIncludedDiagnostics(const std::vector<std::string>& paths)
{
    std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>> result;
    for (const auto& incPath : paths) {
        auto rows = m_symbolDb.diagnosticsForFile(incPath);
        std::vector<ParseError> errs;
        errs.reserve(rows.size());
        for (const auto& r : rows) errs.push_back({r.line, r.col, r.message});
        result.emplace_back(pathToUri(incPath), toDiagnostics(errs));
    }
    return result;
}

void LanguageServer::compileAndPublish(const lsp::DocumentUri& uri)
{
    lsp::Array<lsp::Diagnostic> diags;
    int version = 0;
    // {URI, diagnostics} for every `` `include ``d file touched by this
    // compile -- these were never `didOpen`ed by the client, so they're
    // published separately, with no client-tracked version (plan.md's own
    // "LSP diagnostics-visibility gap": this data was already computed and
    // persisted to the DB per file, just never sent).
    std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>> includedDiags;
    {
        std::lock_guard lock{m_dataMutex};
        if (!m_store.contains(uri))
            return; // closed before this fired (didClose cancels the pending
                     // debounce entry, but guard here too for the narrow race)
        version = m_store.get(uri).version;

        std::vector<std::string> includedFiles;
        diags = parseDiagnostics(uri, m_store.get(uri).text, &includedFiles);
        includedDiags = collectIncludedDiagnostics(includedFiles);
    }
    m_diagnostics.publish(uri, version, diags);
    for (auto& [incUri, incDiags] : includedDiags)
        m_diagnostics.publish(incUri, std::nullopt, std::move(incDiags));
}

std::optional<std::string> LanguageServer::currentTextFor(const std::string& path)
{
    const lsp::DocumentUri uri = pathToUri(path);
    if (m_store.contains(uri))
        return m_store.get(uri).text;
    return readFile(path);
}

void LanguageServer::forceRecompileAndPublish(const std::string& path)
{
    const lsp::DocumentUri uri = pathToUri(path);

    std::optional<int> version;
    lsp::Array<lsp::Diagnostic> diags;
    std::vector<std::pair<lsp::DocumentUri, lsp::Array<lsp::Diagnostic>>> includedDiags;
    {
        std::lock_guard lock{m_dataMutex};
        if (m_store.contains(uri))
            version = m_store.get(uri).version;
        auto text = currentTextFor(path);
        if (!text)
            return; // deleted/unreadable since being scheduled -- nothing to do

        std::vector<std::string> includedFiles;
        auto parseErrors = m_compiler.compile(path, *text, m_projects.configFor(path),
                                               &includedFiles, /*forceRecompile=*/true);
        diags = toDiagnostics(parseErrors);
        includedDiags = collectIncludedDiagnostics(includedFiles);
    }
    m_diagnostics.publish(uri, version, diags);
    for (auto& [incUri, incDiags] : includedDiags)
        m_diagnostics.publish(incUri, std::nullopt, std::move(incDiags));
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

                // Cross-file invalidation (plan.md §6.4): every file whose
                // own compiled unit `` `include ``s the just-saved file needs
                // recompiling too, since its content just changed under
                // them. Scheduled asynchronously rather than recompiled
                // right here -- a widely-`` `include ``d file (e.g. a shared
                // macros header) could have many includers, and this
                // notification handler must return promptly regardless of
                // how large that fan-out is.
                std::vector<std::string> includers;
                {
                    std::lock_guard lock{m_dataMutex};
                    includers = m_symbolDb.includersOf(std::string{uri.path()});
                }
                for (const auto& includerPath : includers)
                    m_dependencyRechecker.schedule(includerPath);
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
            [this](lsp::ReferenceParams&& params) {
                std::lock_guard lock{m_dataMutex};
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_ReferencesResult{nullptr};
                return ReferencesProvider::getReferences(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text,
                    [this](const std::string& path) { return currentTextFor(path); });
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
            [this](lsp::RenameParams&& params) {
                std::lock_guard lock{m_dataMutex};
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_RenameResult{nullptr};
                return RenameProvider::getRename(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text,
                    [this](const std::string& path) { return currentTextFor(path); });
            })
        .add<lsp::requests::TextDocument_SignatureHelp>(
            [this](lsp::SignatureHelpParams&& params) {
                std::lock_guard lock{m_dataMutex};
                if (!m_store.contains(params.textDocument.uri))
                    return lsp::TextDocument_SignatureHelpResult{nullptr};
                return SignatureHelpProvider::getSignatureHelp(
                    params, m_symbolDb, m_store.get(params.textDocument.uri).text);
            });
}
