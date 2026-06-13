#include "lsp/definition.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_DefinitionResult DefinitionProvider::getDefinition(
    const lsp::DefinitionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string word = wordAtPosition(docText,
                                            params.position.line,
                                            params.position.character);
    if (word.empty()) return nullptr;

    const auto rows = db.findSymbolsByName(word);
    if (rows.empty()) return nullptr;

    // Prefer the definition in the same file; fall back to the first match.
    const std::string curPath{params.textDocument.uri.path()};
    const SymbolRow* best = &rows.front();
    for (const auto& r : rows)
        if (r.filePath == curPath) { best = &r; break; }

    lsp::Location loc;
    loc.uri   = pathToUri(best->filePath);
    loc.range = makeRange(best->line, best->col, static_cast<int>(best->name.size()));
    return lsp::Definition{loc};
}
