#include "signature_help.h"

lsp::TextDocument_SignatureHelpResult SignatureHelpProvider::getSignatureHelp(
    const lsp::SignatureHelpParams& /*params*/)
{
    return nullptr;
}
