#include "lsp/hover.h"
#include "lsp/symbol_utils.h"

lsp::TextDocument_HoverResult HoverProvider::getHover(
    const lsp::HoverParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string word = wordAtPosition(docText,
                                            params.position.line,
                                            params.position.character);
    if (word.empty()) return nullptr;

    const auto rows = db.findSymbolsByName(word);
    if (rows.empty()) return nullptr;

    // Prefer a definition in the same file; otherwise use the first match.
    const std::string curPath{params.textDocument.uri.path()};
    const SymbolRow* best = &rows.front();
    for (const auto& r : rows)
        if (r.filePath == curPath) { best = &r; break; }

    std::string content = "**" + best->kind + "** `" + best->name + "`";
    if (!best->detail.empty())
        content += " → `" + best->detail + "`";  // →
    if (!best->scope.empty())
        content += "\n\nin *" + best->scope + "*";

    lsp::Hover hover;
    hover.contents = lsp::MarkupContent{lsp::MarkupKind::Markdown, std::move(content)};
    hover.range    = makeRange(best->line, best->col, static_cast<int>(best->name.size()));
    return hover;
}
