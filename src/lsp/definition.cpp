#include "lsp/definition.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_DefinitionResult DefinitionProvider::getDefinition(
    const lsp::DefinitionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string curPath{params.textDocument.uri.path()};
    const auto resolved = resolveSymbolAt(db, curPath, docText,
                                          params.position.line, params.position.character);
    if (!resolved) return nullptr;
    const SymbolRow& best = resolved->row;

    lsp::Location loc;
    loc.uri   = pathToUri(best.filePath);
    loc.range = makeRange(best.line, best.col, static_cast<int>(best.name.size()));
    return lsp::Definition{loc};
}
