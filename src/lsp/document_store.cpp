#include "document_store.h"
#include <variant>
#include <lsp/uri.h>

void DocumentStore::open(lsp::DidOpenTextDocumentParams params)
{
    const std::string key = params.textDocument.uri.toString();
    std::lock_guard l{m_mutex};
    m_docs[key] = Document{
        .text    = std::move(params.textDocument.text),
        .version = params.textDocument.version,
    };
}

void DocumentStore::update(lsp::DidChangeTextDocumentParams params)
{
    const std::string key = params.textDocument.uri.toString();
    std::lock_guard l{m_mutex};
    auto it = m_docs.find(key);
    if (it == m_docs.end())
        return;  // unknown document — silently ignore per LSP convention

    if (params.contentChanges.empty())
        return;

    // Full-sync: extract the text from whichever change event variant we received.
    // Both TextDocumentContentChangeEvent variants have a 'text' member.
    std::string newText = std::visit(
        [](const auto& change) -> std::string { return change.text; },
        params.contentChanges.front()
    );

    it->second = Document{
        .text    = std::move(newText),
        .version = params.textDocument.version,
    };
}

void DocumentStore::close(lsp::DidCloseTextDocumentParams params)
{
    std::lock_guard l{m_mutex};
    m_docs.erase(params.textDocument.uri.toString());
}

bool DocumentStore::contains(const lsp::DocumentUri& uri) const
{
    std::lock_guard l{m_mutex};
    return m_docs.count(uri.toString()) > 0;
}

DocumentStore::Document DocumentStore::get(const lsp::DocumentUri& uri) const
{
    std::lock_guard l{m_mutex};
    const auto it = m_docs.find(uri.toString());
    if (it == m_docs.end())
        throw std::out_of_range("DocumentStore::get: URI not found: " + uri.toString());
    return it->second;
}

std::vector<lsp::DocumentUri> DocumentStore::uris() const
{
    std::lock_guard l{m_mutex};
    std::vector<lsp::DocumentUri> out;
    out.reserve(m_docs.size());
    for (const auto& [uri, doc] : m_docs) out.push_back(lsp::DocumentUri{lsp::Uri::parse(uri)});
    return out;
}

std::optional<DocumentStore::Document> DocumentStore::find(const lsp::DocumentUri& uri) const
{
    std::lock_guard l{m_mutex};
    const auto it = m_docs.find(uri.toString());
    if (it == m_docs.end()) return std::nullopt;
    return it->second;
}
