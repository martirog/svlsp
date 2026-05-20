#pragma once

#include <lsp/messages.h>

// DefinitionProvider handles textDocument/definition requests.
// Phase 3: always returns null — no symbol resolution until the ANTLR4
// parser (Phase 4) populates the symbol database.
class DefinitionProvider {
public:
    static lsp::TextDocument_DefinitionResult getDefinition(
        const lsp::DefinitionParams& params);
};
