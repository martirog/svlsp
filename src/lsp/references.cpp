#include "references.h"
#include "lsp/macro_resolution.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"
#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace {

// The files an occurrence search reads: the project's, then (with
// `libPaths`, SymbolDatabase::libraryFilePaths) the attached library DBs'
// the project doesn't have itself.
std::vector<std::string> searchedPaths(const SymbolDatabase& db,
                                       const std::vector<std::string>* libPaths)
{
    auto paths = db.allFilePaths();
    if (libPaths) {
        const std::set<std::string> own(paths.begin(), paths.end());
        for (const auto& p : *libPaths)
            if (!own.count(p)) paths.push_back(p);
    }
    return paths;
}

// The first of `declPaths` that only an attached library DB has (one of
// `libPaths` the project doesn't compile itself), or nullopt. The library
// files are only worth searching for such a name: a library never refers to
// what only the project declares (it can't import or extend the project).
std::optional<std::string> libraryDeclaration(const SymbolDatabase& db,
                                              const std::vector<std::string>& libPaths,
                                              const std::vector<std::string>& declPaths)
{
    if (libPaths.empty()) return std::nullopt;
    const auto own = db.allFilePaths();
    for (const auto& p : declPaths)
        if (std::find(libPaths.begin(), libPaths.end(), p) != libPaths.end()
            && std::find(own.begin(), own.end(), p) == own.end())
            return p;
    return std::nullopt;
}

size_t offsetOfOccurrence(const std::string& text, const TextOccurrence& occ)
{
    size_t offset = 0;
    for (int l = 0; l < occ.line; ++l) offset = text.find('\n', offset) + 1;
    return offset + static_cast<size_t>(occ.character);
}

// A macro's occurrences: every macro-shaped use of `name` (see
// macro_resolution.h) in every searched file (searchedPaths). Macros are
// one namespace per compile unit, not scoped, so no per-definition
// filtering: two unrelated same-named `define`s in different files are
// merged. A `define of it
// recorded exactly there is a declaration. std::nullopt if no `define of
// `name` is recorded at all (a command-line or unknown macro).
std::optional<std::vector<ReferencesProvider::Occurrence>> findMacroOccurrences(
    SymbolDatabase& db, const std::string& name,
    const std::function<std::optional<std::string>(const std::string&)>& textForPath,
    std::string* nameOut, bool includeLibraries, std::string* libraryDeclPath)
{
    const auto defs = db.findMacros(name);
    if (defs.empty()) return std::nullopt;
    if (nameOut) *nameOut = name;

    std::vector<std::string> defPaths;
    for (const auto& d : defs) defPaths.push_back(d.filePath);
    auto libPaths = db.libraryFilePaths();
    const auto inLibrary = libraryDeclaration(db, libPaths, defPaths);
    if (libraryDeclPath) *libraryDeclPath = inLibrary.value_or("");
    if (!includeLibraries || !inLibrary) libPaths.clear();

    std::vector<ReferencesProvider::Occurrence> result;
    for (const auto& path : searchedPaths(db, libPaths.empty() ? nullptr : &libPaths)) {
        auto text = textForPath(path);
        if (!text) continue;
        const auto hits = findIdentifierOccurrences(*text, name);
        if (hits.empty()) continue;
        const std::string blanked = blankCommentsAndStrings(*text);
        for (const auto& occ : hits) {
            if (!isMacroOccurrence(blanked, offsetOfOccurrence(*text, occ))) continue;
            bool isDeclaration = false;
            for (const auto& d : defs)
                if (d.filePath == path && d.line - 1 == occ.line && d.col == occ.character)
                    isDeclaration = true;
            result.push_back({path, occ.line, occ.character, isDeclaration});
        }
    }
    return result;
}

} // namespace

std::optional<std::vector<ReferencesProvider::Occurrence>> ReferencesProvider::findOccurrences(
    SymbolDatabase& db, const std::string& curPath, unsigned line, unsigned character,
    const std::string& docText,
    const std::function<std::optional<std::string>(const std::string&)>& textForPath,
    std::string* name, bool includeLibraries, std::string* libraryDeclPath)
{
    if (auto macro = macroNameAt(docText, line, character))
        return findMacroOccurrences(db, macro->name, textForPath, name, includeLibraries,
                                    libraryDeclPath);

    const auto target = resolveSymbolAt(db, curPath, docText, line, character);
    if (!target) return std::nullopt; // not a known symbol -- fail closed
    const std::string word = target->row.name;
    if (name) *name = word;

    const auto declRows = db.findSymbolsByName(word);

    // A declaration's identity across the project DB and the attached ones
    // (row ids are per DB).
    using DeclKey = std::tuple<std::string, int, int, std::string>;
    auto keyOf = [](const SymbolRow& r) { return DeclKey{r.filePath, r.line, r.col, r.name}; };

    // Override-family roots, cached by declaration: every hit on a method
    // resolves to one of a handful of rows.
    std::map<DeclKey, SymbolRow> familyCache;
    auto familyOf = [&](const SymbolRow& row) -> const SymbolRow& {
        auto it = familyCache.find(keyOf(row));
        if (it == familyCache.end())
            it = familyCache.emplace(keyOf(row), overrideFamilyRoot(db, row)).first;
        return it->second;
    };
    const SymbolRow targetRoot = familyOf(target->row);
    const DeclKey targetFamily = keyOf(targetRoot);

    auto libPaths = db.libraryFilePaths();
    const auto inLibrary = libraryDeclaration(db, libPaths, {targetRoot.filePath});
    if (libraryDeclPath) *libraryDeclPath = inLibrary.value_or("");
    if (!includeLibraries || !inLibrary) libPaths.clear();

    std::vector<Occurrence> result;
    for (const auto& path : searchedPaths(db, libPaths.empty() ? nullptr : &libPaths)) {
        auto text = textForPath(path);
        if (!text) continue;

        auto hits = findIdentifierOccurrences(*text, word);
        if (hits.empty()) continue;
        // A same-named macro's uses (`WIDTH next to parameter WIDTH) are
        // never this symbol's.
        const std::string blanked = blankCommentsAndStrings(*text);
        std::erase_if(hits, [&](const TextOccurrence& occ) {
            return isMacroOccurrence(blanked, offsetOfOccurrence(*text, occ));
        });
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
            if (target->exact && r->exact && keyOf(familyOf(r->row)) != targetFamily) continue;

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
                                             docText, textForPath, &word,
                                             /*includeLibraries=*/true);
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
