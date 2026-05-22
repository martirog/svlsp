#pragma once

#include <lsp/messages.h>

// ReferencesProvider handles textDocument/references requests.
// Phase 3: always returns null — no symbol resolution until the ANTLR4
// parser (Phase 4) populates the symbol database.
class ReferencesProvider {
public:
    static lsp::TextDocument_ReferencesResult getReferences(
        const lsp::ReferenceParams& params);
};
