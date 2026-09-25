#pragma once

#include <lsp/messages.h>
#include <functional>
#include <optional>
#include <string>
#include "db/symbol_database.h"

// RenameProvider handles textDocument/rename requests.
//
// Edits exactly the occurrence set ReferencesProvider reports
// (ReferencesProvider::findOccurrences, plan.md §6.30 step B): every hit
// that resolves to the cursor's declaration -- or, for a class method, its
// override family -- plus hits only resolvable by name (see references.h
// for the rules). A same-named but unrelated declaration is left alone.
class RenameProvider {
public:
    // `textForPath(path)` returns the current text for `path` (an open
    // buffer's live text, or disk content for a closed file), or
    // std::nullopt if unavailable. `docText` is the requesting file's own
    // current text, used to resolve the identifier under the cursor.
    //
    // Throws lsp::RequestError (InvalidParams) if `params.newName` isn't a
    // legal SystemVerilog identifier, per the LSP spec's own requirement
    // that an invalid new name be reported as an error response rather than
    // silently producing broken edits.
    static lsp::TextDocument_RenameResult getRename(
        const lsp::RenameParams& params, SymbolDatabase& db,
        const std::string& docText,
        const std::function<std::optional<std::string>(const std::string&)>& textForPath);
};
