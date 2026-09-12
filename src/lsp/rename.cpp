#include "rename.h"
#include "lsp/symbol_utils.h"
#include <lsp/error.h>
#include <cctype>

namespace {
// IEEE 1800 "simple identifier": [a-zA-Z_][a-zA-Z0-9_$]* -- `$` is legal
// mid-identifier (rare in practice) but never as the first character.
bool isValidSvIdentifier(const std::string& name)
{
    if (name.empty()) return false;
    unsigned char first = static_cast<unsigned char>(name[0]);
    if (!std::isalpha(first) && first != '_') return false;
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '$')
            return false;
    }
    return true;
}
} // namespace

lsp::TextDocument_RenameResult RenameProvider::getRename(
    const lsp::RenameParams& params, SymbolDatabase& db,
    const std::string& docText,
    const std::function<std::optional<std::string>(const std::string&)>& textForPath)
{
    if (!isValidSvIdentifier(params.newName)) {
        throw lsp::RequestError(lsp::MessageError::InvalidParams,
            "'" + params.newName + "' is not a valid SystemVerilog identifier");
    }

    const std::string word = wordAtPosition(docText,
                                            params.position.line,
                                            params.position.character);
    if (word.empty()) return nullptr;
    if (db.findSymbolsByName(word).empty()) return nullptr; // not a known symbol

    lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
    for (const auto& path : db.allFilePaths()) {
        auto text = textForPath(path);
        if (!text) continue;

        auto occurrences = findIdentifierOccurrences(*text, word);
        if (occurrences.empty()) continue;

        lsp::Array<lsp::TextEdit> edits;
        for (const auto& occ : occurrences) {
            lsp::TextEdit edit;
            edit.range   = makeRange(occ.line + 1, occ.character, static_cast<int>(word.size()));
            edit.newText = params.newName;
            edits.push_back(std::move(edit));
        }
        changes[pathToUri(path)] = std::move(edits);
    }

    if (changes.empty()) return nullptr;

    lsp::WorkspaceEdit result;
    result.changes = std::move(changes);
    return result;
}
