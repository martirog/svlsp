#include "lsp/hover.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_HoverResult HoverProvider::getHover(
    const lsp::HoverParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string curPath{params.textDocument.uri.path()};
    const auto resolved = resolveSymbolAt(db, curPath, docText,
                                          params.position.line, params.position.character);
    if (!resolved) return nullptr;
    const SymbolRow& best = resolved->row;

    std::string content = "**" + best.kind + "** `" + best.name + "`";
    if (!best.detail.empty())
        content += " → `" + best.detail + "`";  // →
    if (!best.scope.empty())
        content += "\n\nin *" + best.scope + "*";

    lsp::Hover hover;
    hover.contents = lsp::MarkupContent{lsp::MarkupKind::Markdown, std::move(content)};
    hover.range    = makeRange(best.line, best.col, static_cast<int>(best.name.size()));
    return hover;
}
