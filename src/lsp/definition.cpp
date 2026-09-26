#include "lsp/definition.h"
#include "lsp/macro_resolution.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_DefinitionResult DefinitionProvider::getDefinition(
    const lsp::DefinitionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string curPath{params.textDocument.uri.path()};
    // A macro name lands on its `define, never on a same-named symbol.
    if (auto at = macroNameAt(docText, params.position.line, params.position.character)) {
        const auto macro =
            pickMacro(db.findMacros(at->name), curPath, static_cast<int>(at->line) + 1);
        if (!macro) return nullptr;
        lsp::Location loc;
        loc.uri   = pathToUri(macro->filePath);
        loc.range = makeRange(macro->line, macro->col, static_cast<int>(macro->name.size()));
        return lsp::Definition{loc};
    }

    const auto resolved = resolveSymbolAt(db, curPath, docText,
                                          params.position.line, params.position.character);
    if (!resolved) return nullptr;
    const SymbolRow& best = resolved->row;

    lsp::Location loc;
    loc.uri   = pathToUri(best.filePath);
    loc.range = makeRange(best.line, best.col, static_cast<int>(best.name.size()));
    return lsp::Definition{loc};
}
