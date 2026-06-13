#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// WorkspaceSymbolsProvider handles workspace/symbol requests.
class WorkspaceSymbolsProvider {
public:
    static lsp::Workspace_SymbolResult getWorkspaceSymbols(
        const lsp::WorkspaceSymbolParams& params, SymbolDatabase& db);
};
