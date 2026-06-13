#include "lsp/symbol_utils.h"
#include <lsp/fileuri.h>
#include <cctype>

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
    // Walk forward through the text to find the start of the requested line.
    unsigned curLine = 0;
    size_t lineStart = 0;
    while (curLine < line) {
        size_t nl = text.find('\n', lineStart);
        if (nl == std::string::npos) return "";
        lineStart = nl + 1;
        ++curLine;
    }

    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = text.size();

    if (lineStart + character > lineEnd) return "";
    const size_t pos = lineStart + character;

    auto isId = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    };

    size_t start = pos;
    while (start > lineStart && isId(text[start - 1])) --start;
    size_t end = pos;
    while (end < lineEnd && isId(text[end])) ++end;

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
