#include "lsp/workspace_symbols.h"
#include "lsp/symbol_utils.h"

lsp::Workspace_SymbolResult WorkspaceSymbolsProvider::getWorkspaceSymbols(
    const lsp::WorkspaceSymbolParams& params, SymbolDatabase& db)
{
    const auto rows = db.findSymbolsByNamePrefix(params.query);
    if (rows.empty()) return nullptr;

    lsp::Array<lsp::WorkspaceSymbol> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        lsp::WorkspaceSymbol sym;
        sym.name     = row.name;
        sym.kind     = symbolKindFor(row.kind);
        sym.location = lsp::Location{pathToUri(row.filePath),
                                     makeRange(row.line, row.col,
                                               static_cast<int>(row.name.size()))};
        if (!row.parent.empty())
            sym.containerName = row.parent;
        result.push_back(std::move(sym));
    }
    return result;
}
