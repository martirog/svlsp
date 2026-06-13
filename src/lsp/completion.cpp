#include "lsp/completion.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_CompletionResult CompletionProvider::getCompletion(
    const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string path{params.textDocument.uri.path()};
    // LSP position is 0-based; scopeAtPosition uses 1-based lines.
    const int line1 = static_cast<int>(params.position.line) + 1;

    // Extract any partial identifier the user has already typed.
    const std::string prefix = wordAtPosition(docText,
                                              params.position.line,
                                              params.position.character);

    auto rows = db.findSymbolsVisibleAt(path, line1);
    if (rows.empty()) return nullptr;

    lsp::Array<lsp::CompletionItem> items;
    for (auto& row : rows) {
        if (!prefix.empty() &&
            row.name.compare(0, prefix.size(), prefix) != 0)
            continue;

        lsp::CompletionItem item;
        item.label = row.name;
        item.kind  = completionKindFor(row.kind);
        if (!row.detail.empty())
            item.detail = row.detail;
        items.push_back(std::move(item));
    }

    if (items.empty()) return nullptr;
    return items;
}
