#include "lsp/document_symbols.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_DocumentSymbolResult DocumentSymbolsProvider::getDocumentSymbols(
    const lsp::DocumentSymbolParams& params, SymbolDatabase& db)
{
    const std::string path{params.textDocument.uri.path()};
    const auto rows = db.symbolsForFile(path);
    if (rows.empty()) return nullptr;

    lsp::Array<lsp::DocumentSymbol> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        if (row.kind == "Block") continue; // a scope, not a declaration
        if (isForwardTypedef(row)) continue; // the real declaration is listed
        lsp::Range selRange = makeRange(row.line, row.col,
                                        static_cast<int>(row.name.size()));
        // Scope-defining symbols span from their start line to their end line.
        lsp::Range symRange = (row.endLine > 0)
            ? lsp::Range{{static_cast<unsigned>(row.line - 1), 0},
                         {static_cast<unsigned>(row.endLine - 1), 0}}
            : selRange;

        lsp::DocumentSymbol sym;
        sym.name           = row.name;
        sym.kind           = symbolKindFor(row.kind);
        sym.range          = symRange;
        sym.selectionRange = selRange;
        if (std::string detail = displayDetail(row); !detail.empty())
            sym.detail = std::move(detail);
        result.push_back(std::move(sym));
    }
    return result;
}
