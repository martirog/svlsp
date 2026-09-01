#include "lsp/symbol_utils.h"
#include <lsp/fileuri.h>
#include <cctype>

namespace {
bool isDeclarationLikeKind(const std::string& kind)
{
    return kind == "Module" || kind == "Interface" || kind == "Program" ||
           kind == "Package" || kind == "Class" || kind == "Function" || kind == "Task";
}

bool isIdChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// Finds [lineStart, lineEnd) for `line` (0-based) in `text`. Returns false
// (bounds left unset) if `text` has fewer than `line + 1` lines.
bool findLineBounds(const std::string& text, unsigned line, size_t& lineStart, size_t& lineEnd)
{
    unsigned curLine = 0;
    lineStart = 0;
    while (curLine < line) {
        size_t nl = text.find('\n', lineStart);
        if (nl == std::string::npos) return false;
        lineStart = nl + 1;
        ++curLine;
    }
    lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    return true;
}
} // namespace

lsp::SymbolKind symbolKindFor(const std::string& kind)
{
    if (kind == "Module")    return lsp::SymbolKind::Module;
    if (kind == "Interface") return lsp::SymbolKind::Interface;
    if (kind == "Package")   return lsp::SymbolKind::Package;
    if (kind == "Class")     return lsp::SymbolKind::Class;
    if (kind == "Function")  return lsp::SymbolKind::Function;
    if (kind == "Task")      return lsp::SymbolKind::Method;
    if (kind == "Port")      return lsp::SymbolKind::Field;
    if (kind == "Signal")    return lsp::SymbolKind::Variable;
    if (kind == "Parameter") return lsp::SymbolKind::Constant;
    if (kind == "Macro")     return lsp::SymbolKind::Constant;
    return lsp::SymbolKind::Variable;
}

lsp::CompletionItemKind completionKindFor(const std::string& kind)
{
    if (kind == "Module")    return lsp::CompletionItemKind::Module;
    if (kind == "Interface") return lsp::CompletionItemKind::Interface;
    if (kind == "Package")   return lsp::CompletionItemKind::Module;
    if (kind == "Class")     return lsp::CompletionItemKind::Class;
    if (kind == "Function")  return lsp::CompletionItemKind::Function;
    if (kind == "Task")      return lsp::CompletionItemKind::Function;
    if (kind == "Port")      return lsp::CompletionItemKind::Field;
    if (kind == "Signal")    return lsp::CompletionItemKind::Variable;
    if (kind == "Parameter") return lsp::CompletionItemKind::Constant;
    if (kind == "Macro")     return lsp::CompletionItemKind::Keyword;
    return lsp::CompletionItemKind::Variable;
}

std::string wordAtPosition(const std::string& text, unsigned line, unsigned character)
{
    size_t lineStart, lineEnd;
    if (!findLineBounds(text, line, lineStart, lineEnd)) return "";
    if (lineStart + character > lineEnd) return "";
    const size_t pos = lineStart + character;

    size_t start = pos;
    while (start > lineStart && isIdChar(text[start - 1])) --start;
    size_t end = pos;
    while (end < lineEnd && isIdChar(text[end])) ++end;

    return (start < end) ? text.substr(start, end - start) : "";
}

lsp::Range makeRange(int line1, int col0, int nameLen)
{
    const auto l = static_cast<unsigned>(line1 - 1);
    const auto c = static_cast<unsigned>(col0);
    return lsp::Range{{l, c}, {l, c + static_cast<unsigned>(nameLen)}};
}

lsp::DocumentUri pathToUri(const std::string& path)
{
    return lsp::FileUri::fromPath(path);
}

const SymbolRow* pickBestSymbol(const std::vector<SymbolRow>& rows, const std::string& curPath)
{
    for (const auto& r : rows)
        if (r.filePath == curPath) return &r;

    for (const auto& r : rows)
        if (isDeclarationLikeKind(r.kind)) return &r;

    return &rows.front();
}

std::optional<DotCompletion> dotCompletionContext(
    const std::string& text, unsigned line, unsigned character)
{
    size_t lineStart, lineEnd;
    if (!findLineBounds(text, line, lineStart, lineEnd)) return std::nullopt;
    if (lineStart + character > lineEnd) return std::nullopt;
    const size_t pos = lineStart + character;

    // Same left/right identifier walk as wordAtPosition, to recover the
    // (possibly empty) typed member prefix.
    size_t prefixStart = pos;
    while (prefixStart > lineStart && isIdChar(text[prefixStart - 1])) --prefixStart;
    size_t prefixEnd = pos;
    while (prefixEnd < lineEnd && isIdChar(text[prefixEnd])) ++prefixEnd;
    const std::string prefix =
        (prefixStart < prefixEnd) ? text.substr(prefixStart, prefixEnd - prefixStart) : "";

    // No '.' immediately to the left of the prefix -> not a dot-completion.
    if (prefixStart == lineStart || text[prefixStart - 1] != '.') return std::nullopt;

    // Walk left from the '.' to recover the object identifier before it.
    const size_t dotPos = prefixStart - 1;
    size_t objStart = dotPos;
    while (objStart > lineStart && isIdChar(text[objStart - 1])) --objStart;
    if (objStart == dotPos) return std::nullopt; // bare '.', nothing identifier-like before it

    return DotCompletion{text.substr(objStart, dotPos - objStart), prefix};
}
