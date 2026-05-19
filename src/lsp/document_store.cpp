#include "document_store.h"
#include <variant>

void DocumentStore::open(lsp::DidOpenTextDocumentParams params)
{
    const std::string key = params.textDocument.uri.toString();
    m_docs[key] = Document{
        .text    = std::move(params.textDocument.text),
        .version = params.textDocument.version,
    };
}

void DocumentStore::update(lsp::DidChangeTextDocumentParams params)
{
    const std::string key = params.textDocument.uri.toString();
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
    m_docs.erase(params.textDocument.uri.toString());
}

bool DocumentStore::contains(const lsp::DocumentUri& uri) const
{
    return m_docs.count(uri.toString()) > 0;
}

const DocumentStore::Document& DocumentStore::get(const lsp::DocumentUri& uri) const
{
    const auto it = m_docs.find(uri.toString());
    if (it == m_docs.end())
        throw std::out_of_range("DocumentStore::get: URI not found: " + uri.toString());
    return it->second;
}
