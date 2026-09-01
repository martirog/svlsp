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

    const std::string curPath{params.textDocument.uri.path()};
    const SymbolRow* best = pickBestSymbol(rows, curPath);

    lsp::Location loc;
    loc.uri   = pathToUri(best->filePath);
    loc.range = makeRange(best->line, best->col, static_cast<int>(best->name.size()));
    return lsp::Definition{loc};
}
