#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// SignatureHelpProvider handles textDocument/signatureHelp requests.
//
// Scope, disclosed: only module/interface/program instantiation port lists
// are supported (`MyModule u_inst ( ... )`) -- these already have real
// per-port data (name + direction, ParseRecordKind::Port). Function/task
// calls are NOT supported: individual parameters of a function/task are
// never recorded as symbols of their own (only the function/task's own name
// and return type are, see enterFunction_body_declaration/
// enterFunction_prototype in sv_tree_walker.cpp), so there is nothing here
// to build a parameter list from without a new, separate extraction effort
// (capturing a function's own verbatim parameter-list source text) that
// wasn't attempted in this pass.
class SignatureHelpProvider {
public:
    static lsp::TextDocument_SignatureHelpResult getSignatureHelp(
        const lsp::SignatureHelpParams& params, SymbolDatabase& db,
        const std::string& docText);
};
