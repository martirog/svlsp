#include "references.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_ReferencesResult ReferencesProvider::getReferences(
    const lsp::ReferenceParams& params, SymbolDatabase& db,
    const std::string& docText,
    const std::function<std::optional<std::string>(const std::string&)>& textForPath)
{
    const std::string word = wordAtPosition(docText,
                                            params.position.line,
                                            params.position.character);
    if (word.empty()) return nullptr;

    const auto declRows = db.findSymbolsByName(word);
    if (declRows.empty()) return nullptr; // not a known symbol -- fail closed

    lsp::Array<lsp::Location> locations;
    for (const auto& path : db.allFilePaths()) {
        auto text = textForPath(path);
        if (!text) continue;

        for (const auto& occ : findIdentifierOccurrences(*text, word)) {
            bool isDeclaration = false;
            for (const auto& d : declRows) {
                if (d.filePath == path && d.line - 1 == occ.line && d.col == occ.character) {
                    isDeclaration = true;
                    break;
                }
            }
            if (isDeclaration && !params.context.includeDeclaration) continue;

            lsp::Location loc;
            loc.uri   = pathToUri(path);
            loc.range = makeRange(occ.line + 1, occ.character, static_cast<int>(word.size()));
            locations.push_back(std::move(loc));
        }
    }

    if (locations.empty()) return nullptr;
    return locations;
}
