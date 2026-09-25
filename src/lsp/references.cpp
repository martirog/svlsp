#include "references.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"
#include <map>

std::optional<std::vector<ReferencesProvider::Occurrence>> ReferencesProvider::findOccurrences(
    SymbolDatabase& db, const std::string& curPath, unsigned line, unsigned character,
    const std::string& docText,
    const std::function<std::optional<std::string>(const std::string&)>& textForPath,
    std::string* name)
{
    const auto target = resolveSymbolAt(db, curPath, docText, line, character);
    if (!target) return std::nullopt; // not a known symbol -- fail closed
    const std::string word = target->row.name;
    if (name) *name = word;

    const auto declRows = db.findSymbolsByName(word);

    // Override-family ids, cached by row id: every hit on a method resolves
    // to one of a handful of rows.
    std::map<int64_t, int64_t> familyCache;
    auto familyOf = [&](const SymbolRow& row) {
        auto [it, inserted] = familyCache.try_emplace(row.id, 0);
        if (inserted) it->second = overrideFamilyId(db, row);
        return it->second;
    };
    const int64_t targetFamily = familyOf(target->row);

    std::vector<Occurrence> result;
    for (const auto& path : db.allFilePaths()) {
        auto text = textForPath(path);
        if (!text) continue;

        const auto hits = findIdentifierOccurrences(*text, word);
        if (hits.empty()) continue;
        std::vector<std::pair<unsigned, unsigned>> positions;
        positions.reserve(hits.size());
        for (const auto& occ : hits)
            positions.emplace_back(static_cast<unsigned>(occ.line),
                                   static_cast<unsigned>(occ.character));
        const auto resolved = resolveSymbolsAt(db, path, *text, positions);

        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& r = resolved[i];
            if (!r) continue;
            // A fallback target is only a name-only guess, so it can't rule
            // out an exactly-resolved hit either.
            if (target->exact && r->exact && familyOf(r->row) != targetFamily) continue;

            const auto& occ = hits[i];
            bool isDeclaration = false;
            for (const auto& d : declRows) {
                if (d.filePath == path && d.line - 1 == occ.line && d.col == occ.character) {
                    isDeclaration = true;
                    break;
                }
            }
            result.push_back({path, occ.line, occ.character, isDeclaration});
        }
    }
    return result;
}

lsp::TextDocument_ReferencesResult ReferencesProvider::getReferences(
    const lsp::ReferenceParams& params, SymbolDatabase& db,
    const std::string& docText,
    const std::function<std::optional<std::string>(const std::string&)>& textForPath)
{
    std::string word;
    const auto occurrences = findOccurrences(db, std::string{params.textDocument.uri.path()},
                                             params.position.line, params.position.character,
                                             docText, textForPath, &word);
    if (!occurrences) return nullptr;

    lsp::Array<lsp::Location> locations;
    for (const auto& occ : *occurrences) {
        if (occ.isDeclaration && !params.context.includeDeclaration) continue;
        lsp::Location loc;
        loc.uri   = pathToUri(occ.path);
        loc.range = makeRange(occ.line + 1, occ.character, static_cast<int>(word.size()));
        locations.push_back(std::move(loc));
    }

    if (locations.empty()) return nullptr;
    return locations;
}
