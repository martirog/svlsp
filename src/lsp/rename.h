#pragma once

#include <lsp/messages.h>

// RenameProvider handles textDocument/rename requests.
// Phase 3: always returns null — no rename edits until the ANTLR4 parser
// (Phase 4) can resolve all reference sites for the symbol being renamed.
class RenameProvider {
public:
    static lsp::TextDocument_RenameResult getRename(const lsp::RenameParams& params);
};
