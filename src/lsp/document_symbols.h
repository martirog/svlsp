#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// DocumentSymbolsProvider handles textDocument/documentSymbol requests.
class DocumentSymbolsProvider {
public:
    static lsp::TextDocument_DocumentSymbolResult getDocumentSymbols(
        const lsp::DocumentSymbolParams& params, SymbolDatabase& db);
};
