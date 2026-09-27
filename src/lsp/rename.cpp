#include "rename.h"
#include "lsp/references.h"
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

    std::string word;
    const auto occurrences = ReferencesProvider::findOccurrences(
        db, std::string{params.textDocument.uri.path()}, params.position.line,
        params.position.character, docText, textForPath, &word);
    if (!occurrences) return nullptr; // not a known symbol
    if (word == "new") {
        throw lsp::RequestError(lsp::MessageError::InvalidParams,
            "a class constructor is always named 'new' and can't be renamed");
    }

    lsp::Map<lsp::DocumentUri, lsp::Array<lsp::TextEdit>> changes;
    for (const auto& occ : *occurrences) {
        lsp::TextEdit edit;
        edit.range   = makeRange(occ.line + 1, occ.character, static_cast<int>(word.size()));
        edit.newText = params.newName;
        changes[pathToUri(occ.path)].push_back(std::move(edit));
    }

    if (changes.empty()) return nullptr;

    lsp::WorkspaceEdit result;
    result.changes = std::move(changes);
    return result;
}
