#pragma once

#include <lsp/messages.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "db/symbol_database.h"

// ReferencesProvider handles textDocument/references requests.
//
// There is no reference-tracking table in this schema, only declarations
// (see plan.md §6.21's own research note on the same underlying gap), so the
// candidates are a lexical whole-word search across every file the DB knows
// about (symbol_utils.h's findIdentifierOccurrences; comments and string
// literals excluded). Each candidate is then resolved with
// resolveSymbolsAt (plan.md §6.30 step B) and kept only if it refers to
// the same declaration as the cursor:
//   - an exact resolution must land on the cursor's own row, or for a
//     class method on the same override family (overrideFamilyId: an
//     override, or the base method it overrides), so a same-named signal in
//     another module, a local shadowing a field, or an unrelated class's
//     same-named member is dropped;
//   - a name-only fallback resolution (exact=false: a struct or
//     hierarchical receiver, an unknown package, ...) is kept, as before
//     step B, rather than silently dropping a real use it can't type;
//   - an understood qualifier/receiver with no such member (nullopt) is
//     dropped.
// If the cursor itself only resolves by fallback, every resolvable hit is
// kept (the pre-step-B lexical behavior).
class ReferencesProvider {
public:
    // `textForPath(path)` returns the current text for `path` (an open
    // buffer's live text, or disk content for a closed file), or
    // std::nullopt if unavailable (e.g. deleted since last compiled).
    // `docText` is the requesting file's own current text, used to resolve
    // the identifier under the cursor.
    static lsp::TextDocument_ReferencesResult getReferences(
        const lsp::ReferenceParams& params, SymbolDatabase& db,
        const std::string& docText,
        const std::function<std::optional<std::string>(const std::string&)>& textForPath);

    // One kept occurrence: 0-based LSP line/character of its first
    // character, and whether a declaration of the name sits exactly there.
    struct Occurrence {
        std::string path;
        int         line;
        int         character;
        bool        isDeclaration;
    };

    // The filtered occurrence set both references and rename use (rename
    // must edit exactly what references reports). The searched name is
    // written to `*name`. std::nullopt if the cursor isn't on a symbol
    // that resolves to anything.
    static std::optional<std::vector<Occurrence>> findOccurrences(
        SymbolDatabase& db, const std::string& curPath, unsigned line, unsigned character,
        const std::string& docText,
        const std::function<std::optional<std::string>(const std::string&)>& textForPath,
        std::string* name);
};
