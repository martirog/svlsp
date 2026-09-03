#include "lsp/symbol_utils.h"
#include <lsp/fileuri.h>
#include <algorithm>
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

namespace {
// Finds the `openCh` matching the `closeCh` at text[closePos], walking left
// with a balance counter (seeded at 1 for the close delimiter already
// known) and a minimal in-string state so a '.'/openCh/closeCh inside a
// string-literal argument (e.g. foo("a.b"), aa["a.b"]) never perturbs the
// count. Returns std::string::npos if no balanced match exists before
// lineStart. Shared by call-paren matching (§6.14) and index-bracket
// matching (§6.15) -- same technique, different delimiter pair.
size_t matchingOpenDelim(const std::string& text, size_t lineStart, size_t closePos,
                          char openCh, char closeCh)
{
    size_t i = closePos;
    int depth = 1;
    bool inString = false;
    while (i > lineStart) {
        char c = text[i - 1];
        if (inString) {
            if (c == '"' && (i - 1 == lineStart || text[i - 2] != '\\')) inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == closeCh) {
            ++depth;
        } else if (c == openCh) {
            --depth;
            if (depth == 0) return i - 1;
        }
        --i;
    }
    return std::string::npos;
}
} // namespace

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

    // Walk left extracting one chain segment at a time (rightmost first),
    // stopping once a segment isn't immediately preceded by another '.'.
    std::vector<ChainSegment> segments;
    size_t dotPos = prefixStart - 1; // position of the '.' before the next segment

    while (true) {
        if (dotPos == lineStart) return std::nullopt; // bare '.', nothing before it

        const size_t segEnd = dotPos; // exclusive end of this segment's text
        std::string name;
        bool isCall = false;
        int indexDepth = 0;
        size_t segStart;

        if (text[segEnd - 1] == ')') {
            const size_t openParen = matchingOpenDelim(text, lineStart, segEnd - 1, '(', ')');
            if (openParen == std::string::npos || openParen == lineStart) return std::nullopt;
            size_t idEnd = openParen;
            while (idEnd > lineStart && (text[idEnd - 1] == ' ' || text[idEnd - 1] == '\t')) --idEnd;
            size_t idStart = idEnd;
            while (idStart > lineStart && isIdChar(text[idStart - 1])) --idStart;
            if (idStart == idEnd) return std::nullopt; // "(...)" with no name before it
            name = text.substr(idStart, idEnd - idStart);
            isCall = true;
            segStart = idStart;
        } else if (text[segEnd - 1] == ']') {
            // Walk left over one or more consecutive "[...]" groups
            // (arr[i][j] is one segment with indexDepth=2, not two segments
            // -- there's no '.' between the brackets).
            size_t cursor = segEnd;
            while (cursor > lineStart && text[cursor - 1] == ']') {
                const size_t openBracket = matchingOpenDelim(text, lineStart, cursor - 1, '[', ']');
                if (openBracket == std::string::npos || openBracket == lineStart) return std::nullopt;
                ++indexDepth;
                cursor = openBracket;
            }
            size_t idEnd = cursor;
            size_t idStart = idEnd;
            while (idStart > lineStart && isIdChar(text[idStart - 1])) --idStart;
            if (idStart == idEnd) return std::nullopt; // "[...]" with no name before it
            name = text.substr(idStart, idEnd - idStart);
            segStart = idStart;
        } else if (isIdChar(text[segEnd - 1])) {
            segStart = segEnd;
            while (segStart > lineStart && isIdChar(text[segStart - 1])) --segStart;
            name = text.substr(segStart, segEnd - segStart);
        } else {
            return std::nullopt; // nothing identifier/call/index-like before the dot
        }

        segments.push_back({std::move(name), isCall, indexDepth});

        if (segStart == lineStart || text[segStart - 1] != '.') break; // segment 0 reached
        dotPos = segStart - 1;
    }

    std::reverse(segments.begin(), segments.end());
    return DotCompletion{std::move(segments), prefix};
}
