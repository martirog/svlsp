#pragma once

#include <lsp/messages.h>

// DocumentSymbolsProvider handles textDocument/documentSymbol requests.
// Phase 3: always returns null — no symbol outline until the ANTLR4
// parser (Phase 4) extracts module/interface/function/task declarations.
class DocumentSymbolsProvider {
public:
    static lsp::TextDocument_DocumentSymbolResult getDocumentSymbols(
        const lsp::DocumentSymbolParams& params);
};
