#include "lsp/hover.h"
#include "lsp/macro_resolution.h"
#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"

namespace {

// A macro body can be a whole UVM class skeleton on one joined line.
constexpr size_t kMaxMacroBody = 400;

lsp::TextDocument_HoverResult macroHover(const MacroNameAt& at, SymbolDatabase& db,
                                         const std::string& curPath)
{
    const auto macro = pickMacro(db.findMacros(at.name), curPath, static_cast<int>(at.line) + 1);
    if (!macro) return nullptr;

    std::string content = "**Macro** `` " + macroSignature(*macro) + " ``";
    if (!macro->doc.empty()) content += "\n\n" + docMarkdown(macro->doc);
    if (!macro->body.empty()) {
        std::string body = macro->body;
        if (body.size() > kMaxMacroBody) body = body.substr(0, kMaxMacroBody) + " …";
        content += "\n\n```systemverilog\n" + body + "\n```";
    }
    content += "\n\ndefined at *" + macro->filePath + ":" + std::to_string(macro->line) + "*";

    lsp::Hover hover;
    hover.contents = lsp::MarkupContent{lsp::MarkupKind::Markdown, std::move(content)};
    hover.range    = makeRange(static_cast<int>(at.line) + 1, static_cast<int>(at.character),
                               static_cast<int>(at.name.size()));
    return hover;
}

} // namespace

lsp::TextDocument_HoverResult HoverProvider::getHover(
    const lsp::HoverParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string curPath{params.textDocument.uri.path()};
    // A macro name never falls through to a same-named symbol.
    if (auto macro = macroNameAt(docText, params.position.line, params.position.character))
        return macroHover(*macro, db, curPath);

    const auto resolved = resolveSymbolAt(db, curPath, docText,
                                          params.position.line, params.position.character);
    if (!resolved) return nullptr;
    const SymbolRow& best = resolved->row;

    std::string content = "**" + best.kind + "** `" + best.name + "`";
    if (!best.detail.empty())
        content += " → `" + best.detail + "`";  // →
    if (const std::string doc = symbolDoc(db, best); !doc.empty())
        content += "\n\n" + docMarkdown(doc);
    if (!best.scope.empty())
        content += "\n\nin *" + best.scope + "*";

    lsp::Hover hover;
    hover.contents = lsp::MarkupContent{lsp::MarkupKind::Markdown, std::move(content)};
    hover.range    = makeRange(best.line, best.col, static_cast<int>(best.name.size()));
    return hover;
}
