#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <lsp/messages.h>

// DocumentStore tracks the content of all open text documents for the server.
// State changes arrive via LSP notifications (didOpen, didChange, didClose).
// Content is stored verbatim; callers (future LSP handlers) query by URI.
// Thread-safe: requests may be answered off the message thread (plan.md
// §6.34 S4), so every access locks, and documents are returned by value.
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
    Document get(const lsp::DocumentUri& uri) const;

    // The stored document, or nullopt.
    std::optional<Document> find(const lsp::DocumentUri& uri) const;

    // Every open document's URI.
    std::vector<lsp::DocumentUri> uris() const;

private:
    mutable std::mutex m_mutex;
    std::map<std::string, Document> m_docs;  // keyed by uri.toString()
};
