#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// HoverProvider handles textDocument/hover requests.
class HoverProvider {
public:
    static lsp::TextDocument_HoverResult getHover(
        const lsp::HoverParams& params, SymbolDatabase& db, const std::string& docText);
};
