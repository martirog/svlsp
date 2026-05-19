#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <lsp/messages.h>

// DocumentStore tracks the content of all open text documents for the server.
// State changes arrive via LSP notifications (didOpen, didChange, didClose).
// Content is stored verbatim; callers (future LSP handlers) query by URI.
class DocumentStore {
public:
    struct Document {
        std::string text;
        int         version{0};
    };

    void open(lsp::DidOpenTextDocumentParams params);
    void update(lsp::DidChangeTextDocumentParams params);
    void close(lsp::DidCloseTextDocumentParams params);

    bool contains(const lsp::DocumentUri& uri) const;

    // Returns the stored document. Throws std::out_of_range if URI is unknown.
    const Document& get(const lsp::DocumentUri& uri) const;

private:
    std::map<std::string, Document> m_docs;  // keyed by uri.toString()
};
