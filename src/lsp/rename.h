#pragma once

#include <lsp/messages.h>
#include <functional>
#include <optional>
#include <string>
#include "db/symbol_database.h"

// RenameProvider handles textDocument/rename requests.
//
// Built on the same lexical, name-based search ReferencesProvider uses (see
// its own doc comment for the scope this implies) -- renaming replaces
// every occurrence of the name across every file the DB knows about,
// including ones outside the intended symbol's actual scope if another,
// unrelated declaration happens to share the name.
class RenameProvider {
public:
    // `textForPath(path)` returns the current text for `path` (an open
    // buffer's live text, or disk content for a closed file), or
    // std::nullopt if unavailable. `docText` is the requesting file's own
    // current text, used only to resolve the word under the cursor.
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
