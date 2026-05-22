#pragma once

#include <lsp/messages.h>

// CompletionProvider handles textDocument/completion requests.
// Phase 3: always returns null — no completion candidates until the ANTLR4
// parser (Phase 4) populates the symbol database.
class CompletionProvider {
public:
    static lsp::TextDocument_CompletionResult getCompletion(
        const lsp::CompletionParams& params);
};
