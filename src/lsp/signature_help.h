#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// SignatureHelpProvider handles textDocument/signatureHelp requests.
//
// Two supported call-header shapes, tried in order (plan.md §6.22 +
// its own follow-up section):
//   1. Module/interface/program instantiation port lists
//      (`MyModule u_inst ( ... )`) -- ParseRecordKind::Port rows recorded by
//      enterAnsi_port_declaration (name + direction only).
//   2. Bare (undotted) function/task calls (`my_func( ... )`, including a
//      class method called bare from inside its own class) --
//      ParseRecordKind::Port rows recorded by enterTf_port_item
//      (sv_tree_walker.cpp), reusing the same kind since a function/task
//      parameter is semantically the same shape (name + direction + type,
//      plus an optional default value rendered after the name -- see
//      portLabel() below and PARAM_DEFAULT_VALUE_SEP in parse_record.h).
//
// Disclosed scope limit: a *dotted* call (`obj.method(`) is NOT supported --
// resolving it needs the same chain-resolution machinery dot-completion
// already has (completion.cpp's resolveChain/resolveMemberSegment), which
// this file's lexical scan deliberately doesn't attempt; parseCallHeader
// fails closed for this shape rather than guessing by method name alone.
class SignatureHelpProvider {
public:
    static lsp::TextDocument_SignatureHelpResult getSignatureHelp(
        const lsp::SignatureHelpParams& params, SymbolDatabase& db,
        const std::string& docText);
};
