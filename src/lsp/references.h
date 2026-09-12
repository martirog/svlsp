#pragma once

#include <lsp/messages.h>
#include <functional>
#include <optional>
#include <string>
#include "db/symbol_database.h"

// ReferencesProvider handles textDocument/references requests.
//
// Scope, disclosed: there is no reference-tracking table in this schema,
// only declarations (see plan.md §6.21's own research note on the same
// underlying gap) -- this is a lexical, name-based search across every file
// the DB knows about (symbol_utils.h's findIdentifierOccurrences), not a
// scope-aware one. Comments and string literals are excluded, but two
// unrelated declarations that happen to share a name (e.g. two classes both
// named `Packet` in different files) are indistinguishable from here and
// both get reported. A deliberately pragmatic v1, not the "real" one.
class ReferencesProvider {
public:
    // `textForPath(path)` returns the current text for `path` (an open
    // buffer's live text, or disk content for a closed file), or
    // std::nullopt if unavailable (e.g. deleted since last compiled).
    // `docText` is the requesting file's own current text, used only to
    // resolve the word under the cursor.
    static lsp::TextDocument_ReferencesResult getReferences(
        const lsp::ReferenceParams& params, SymbolDatabase& db,
        const std::string& docText,
        const std::function<std::optional<std::string>(const std::string&)>& textForPath);
};
