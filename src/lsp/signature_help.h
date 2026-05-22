#pragma once

#include <lsp/messages.h>

// SignatureHelpProvider handles textDocument/signatureHelp requests.
// Phase 3: always returns null — no signature information until the ANTLR4
// parser (Phase 4) can resolve module port lists and function signatures.
class SignatureHelpProvider {
public:
    static lsp::TextDocument_SignatureHelpResult getSignatureHelp(
        const lsp::SignatureHelpParams& params);
};
