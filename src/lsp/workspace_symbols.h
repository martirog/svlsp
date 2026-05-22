#pragma once

#include <lsp/messages.h>

// WorkspaceSymbolsProvider handles workspace/symbol requests.
// Phase 3: always returns null — no cross-file symbol search until the ANTLR4
// parser (Phase 4) populates the symbol database.
class WorkspaceSymbolsProvider {
public:
    static lsp::Workspace_SymbolResult getWorkspaceSymbols(
        const lsp::WorkspaceSymbolParams& params);
};
