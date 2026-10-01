#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// SignatureHelpProvider handles textDocument/signatureHelp requests.
//
// A macro invocation (`` `uvm_info( ``, plan.md §6.29 part A) is checked
// first: the macro's parameters come from SymbolDatabase::findMacros (the
// macros table, filled from the preprocessor's `define records). It never
// falls through -- an unknown or object-like macro gets null, never a
// same-named function or keyword.
//
// Next, a keyword construct's header (`for (`, `foreach (`, `case (`,
// assertion forms, `randomize(`; plan.md §6.29 part B), from the static
// table in lsp/sv_keyword_signatures.h -- a keyword is never a
// user-declared name. Otherwise, these call-header shapes, tried in order
// (plan.md §6.22 + its own follow-up section, plus §6.27 for the third):
//   1. Module/interface/program instantiation port lists
//      (`MyModule u_inst ( ... )`) -- ParseRecordKind::Port rows recorded by
//      enterAnsi_port_declaration (name + direction only).
//   2. Bare (undotted) function/task calls (`my_func( ... )`, including a
//      class method called bare from inside its own class, and a
//      `Class::`/`pkg::`-qualified call) -- ParseRecordKind::Port rows
//      recorded by enterTf_port_item (sv_tree_walker.cpp), reusing the
//      same kind since a function/task parameter is semantically the same
//      shape (name + direction + type, plus an optional default value
//      rendered after the name -- see portLabel() below and
//      PARAM_DEFAULT_VALUE_SEP in parse_record.h).
//   3. Dotted calls (`obj.method(`, or a longer chain `obj.field.method(`).
//   Shapes 2 and 3 resolve the call name with resolveSymbolAt
//   (lsp/symbol_resolution.h), the resolver hover and definition use, and
//   keep only an exact Function/Task. A qualified or dotted call fails
//   closed (null) otherwise (plan.md §6.26); an unqualified one nothing
//   visible declares falls back to a name-only Function/Task search.
//   4. System tasks/functions (`$display(`, `$clog2(`, plan.md §6.29 part
//      C) -- an unqualified `$name(` bare call that finds no Function/Task
//      row falls back to the static table in lsp/sv_system_tasks.h. A name
//      not in the table (a vendor/PLI task) stays null.
class SignatureHelpProvider {
public:
    static lsp::TextDocument_SignatureHelpResult getSignatureHelp(
        const lsp::SignatureHelpParams& params, SymbolDatabase& db,
        const std::string& docText);
};
