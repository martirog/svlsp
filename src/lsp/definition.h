#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// DefinitionProvider handles textDocument/definition requests.
class DefinitionProvider {
public:
    static lsp::TextDocument_DefinitionResult getDefinition(
        const lsp::DefinitionParams& params, SymbolDatabase& db, const std::string& docText);
};
