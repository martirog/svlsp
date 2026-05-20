#pragma once

#include <lsp/messages.h>

// HoverProvider handles textDocument/hover requests.
// Phase 3: always returns null — no symbol information until the ANTLR4
// parser (Phase 4) populates the symbol database.
class HoverProvider {
public:
    static lsp::TextDocument_HoverResult getHover(const lsp::HoverParams& params);
};
