#include "lsp/workspace_symbols.h"
#include "lsp/symbol_utils.h"

lsp::Workspace_SymbolResult WorkspaceSymbolsProvider::getWorkspaceSymbols(
    const lsp::WorkspaceSymbolParams& params, SymbolDatabase& db)
{
    // A query written the way a macro is used (`` `name ``) matches macros
    // only.
    const bool macrosOnly = !params.query.empty() && params.query.front() == '`';
    const std::string query = macrosOnly ? params.query.substr(1) : params.query;
    const auto rows   = macrosOnly ? std::vector<SymbolRow>{} : db.findSymbolsByNamePrefix(query);
    const auto macros = db.findMacrosByNamePrefix(query);
    if (rows.empty() && macros.empty()) return nullptr;

    lsp::Array<lsp::WorkspaceSymbol> result;
    result.reserve(rows.size() + macros.size());
    for (const auto& row : rows) {
        if (row.kind == "Block") continue; // a scope, not a declaration to jump to
        lsp::WorkspaceSymbol sym;
        sym.name     = row.name;
        sym.kind     = symbolKindFor(row.kind);
        sym.location = lsp::Location{pathToUri(row.filePath),
                                     makeRange(row.line, row.col,
                                               static_cast<int>(row.name.size()))};
        // An unnamed block isn't a name: its nearest named scope contains it.
        if (!row.parent.empty() && row.parent.front() != '@') {
            sym.containerName = row.parent;
        } else if (!row.parent.empty()) {
            const std::string scope = displayScope(row.scope);
            const auto sep = scope.rfind("::");
            if (!scope.empty())
                sym.containerName = sep == std::string::npos ? scope : scope.substr(sep + 2);
        }
        result.push_back(std::move(sym));
    }
    // Macros live in their own table (plan.md §6.29), never in `symbols`.
    for (const auto& m : macros) {
        lsp::WorkspaceSymbol sym;
        sym.name          = m.name;
        sym.kind          = symbolKindFor("Macro");
        sym.location      = lsp::Location{pathToUri(m.filePath),
                                          makeRange(m.line, m.col, static_cast<int>(m.name.size()))};
        sym.containerName = "`define";
        result.push_back(std::move(sym));
    }
    return result;
}
