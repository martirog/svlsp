#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// CompletionProvider handles textDocument/completion requests.
class CompletionProvider {
public:
    static lsp::TextDocument_CompletionResult getCompletion(
        const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText);
};
