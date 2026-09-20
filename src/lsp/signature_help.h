#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// SignatureHelpProvider handles textDocument/signatureHelp requests.
//
// Three supported call-header shapes, tried in order (plan.md §6.22 + its
// own follow-up section, plus §6.27 for the third):
//   1. Module/interface/program instantiation port lists
//      (`MyModule u_inst ( ... )`) -- ParseRecordKind::Port rows recorded by
//      enterAnsi_port_declaration (name + direction only).
//   2. Bare (undotted) function/task calls (`my_func( ... )`, including a
//      class method called bare from inside its own class, and a
//      `Class::`/`pkg::`-qualified call resolved via
//      SymbolDatabase::resolveMethod, plan.md §6.26) -- ParseRecordKind::Port
//      rows recorded by enterTf_port_item (sv_tree_walker.cpp), reusing the
//      same kind since a function/task parameter is semantically the same
//      shape (name + direction + type, plus an optional default value
//      rendered after the name -- see portLabel() below and
//      PARAM_DEFAULT_VALUE_SEP in parse_record.h).
//   3. Dotted calls (`obj.method(`, or a longer chain
//      `obj.field.method(`) -- parseDottedCallHeader (signature_help.cpp)
//      reuses dot-completion's own chain-resolution machinery
//      (dotCompletionContext/resolveChain, lsp/symbol_utils.h) to resolve
//      the receiver's declared type, then SymbolDatabase::resolveMethod to
//      find the method on that type or one of its ancestors via `extends`.
//      Fails closed (null) if the receiver chain or the method itself
//      doesn't resolve, same posture as every other shape here.
class SignatureHelpProvider {
public:
    static lsp::TextDocument_SignatureHelpResult getSignatureHelp(
        const lsp::SignatureHelpParams& params, SymbolDatabase& db,
        const std::string& docText);
};
