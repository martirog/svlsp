#include "lsp/symbol_utils.h"
#include "lsp/sv_builtin_methods.h"
#include <lsp/fileuri.h>
#include <algorithm>
#include <cctype>
#include <unordered_set>

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
    if (kind == "Typedef")     return lsp::SymbolKind::TypeParameter;
    if (kind == "EnumLiteral") return lsp::SymbolKind::EnumMember;
    if (kind == "Member")      return lsp::SymbolKind::Field;
    if (kind == "Genvar")      return lsp::SymbolKind::Variable;
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
    if (kind == "Typedef")     return lsp::CompletionItemKind::TypeParameter;
    if (kind == "EnumLiteral") return lsp::CompletionItemKind::EnumMember;
    if (kind == "Member")      return lsp::CompletionItemKind::Field;
    if (kind == "Genvar")      return lsp::CompletionItemKind::Variable;
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

std::vector<TextOccurrence> findIdentifierOccurrences(
    const std::string& text, const std::string& name)
{
    std::vector<TextOccurrence> result;
    if (name.empty()) return result;

    bool inBlockComment = false;
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        const size_t lineEnd = (eol == std::string::npos) ? text.size() : eol;

        for (size_t i = pos; i < lineEnd; ) {
            if (inBlockComment) {
                size_t end = text.find("*/", i);
                if (end == std::string::npos || end >= lineEnd) { i = lineEnd; break; }
                inBlockComment = false;
                i = end + 2;
                continue;
            }
            if (i + 1 < lineEnd && text[i] == '/' && text[i + 1] == '*') {
                inBlockComment = true;
                i += 2;
                continue;
            }
            if (i + 1 < lineEnd && text[i] == '/' && text[i + 1] == '/')
                break; // rest of the line is a line comment
            if (text[i] == '"') {
                ++i;
                while (i < lineEnd && text[i] != '"') {
                    if (text[i] == '\\' && i + 1 < lineEnd) i += 2;
                    else ++i;
                }
                if (i < lineEnd) ++i; // closing quote
                continue;
            }
            if (isIdChar(text[i]) && !std::isdigit(static_cast<unsigned char>(text[i]))) {
                size_t start = i;
                while (i < lineEnd && isIdChar(text[i])) ++i;
                if (i - start == name.size() && text.compare(start, i - start, name) == 0)
                    result.push_back({lineNo, static_cast<int>(start - pos)});
                continue;
            }
            ++i;
        }

        if (eol == std::string::npos) break;
        pos = eol + 1;
        ++lineNo;
    }
    return result;
}

lsp::Position positionForOffset(const std::string& text, size_t offset)
{
    offset = std::min(offset, text.size());
    unsigned line = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') { ++line; lineStart = i + 1; }
    }
    return lsp::Position{line, static_cast<unsigned>(offset - lineStart)};
}

std::optional<size_t> offsetForPosition(const std::string& text, unsigned line,
                                        unsigned character)
{
    size_t offset = 0;
    for (unsigned cur = 0; cur < line; ++cur) {
        const size_t nl = text.find('\n', offset);
        if (nl == std::string::npos) return std::nullopt;
        offset = nl + 1;
    }
    offset += character;
    return offset <= text.size() ? std::optional<size_t>(offset) : std::nullopt;
}

namespace {
// Position of the next single ':' layer separator at or after `from`,
// skipping every "::" (a `pkg::`/`Outer::` qualifier inside one layer's
// type name, plan.md §6.30 step A). npos if there is none.
size_t nextLayerSeparator(const std::string& detail, size_t from)
{
    for (size_t i = from; i < detail.size(); ++i) {
        if (detail[i] != ':') continue;
        if (i + 1 < detail.size() && detail[i + 1] == ':') { ++i; continue; }
        return i;
    }
    return std::string::npos;
}

// A container/element detail string is a ':'-delimited list of layers,
// outermost first (plan.md §6.15 -- see containerDimensionTags() in
// src/compiler/sv_tree_walker.cpp for how it's built). Splits on single
// ':' only; a qualified type name's "::" stays inside its layer.
std::vector<std::string> splitLayers(const std::string& detail)
{
    std::vector<std::string> layers;
    size_t start = 0;
    while (start <= detail.size()) {
        size_t colon = nextLayerSeparator(detail, start);
        if (colon == std::string::npos) {
            layers.push_back(detail.substr(start));
            break;
        }
        layers.push_back(detail.substr(start, colon - start));
        start = colon + 1;
    }
    return layers;
}

bool isContainerDimensionTag(const std::string& layer)
{
    return layer == CONTAINER_QUEUE || layer == CONTAINER_ASSOC ||
           layer == CONTAINER_DYNAMIC_ARRAY || layer == CONTAINER_FIXED_ARRAY;
}
} // namespace

std::string firstTypeLayer(const std::string& detail)
{
    const size_t colon = nextLayerSeparator(detail, 0);
    return colon == std::string::npos ? detail : detail.substr(0, colon);
}

std::string peelDimensionLayers(const std::string& detail, int depth)
{
    if (depth <= 0) return detail;
    std::vector<std::string> layers = splitLayers(detail);
    int dimCount = 0;
    while (dimCount < static_cast<int>(layers.size()) && isContainerDimensionTag(layers[dimCount]))
        ++dimCount;
    if (depth > dimCount) return "";

    std::string result;
    for (size_t i = static_cast<size_t>(depth); i < layers.size(); ++i) {
        if (i > static_cast<size_t>(depth)) result += ':';
        result += layers[i];
    }
    return result;
}

std::vector<SymbolRow> membersAcrossChain(SymbolDatabase& db, const std::string& className,
                                           const std::string& curPath)
{
    std::vector<SymbolRow> members;
    std::unordered_set<std::string> seen;
    for (auto& scope : db.baseClassChain(className, curPath))
        for (auto& row : db.findSymbolsInScope(scope))
            if (seen.insert(row.name).second) members.push_back(row);
    return members;
}

std::string resolveFirstSegment(SymbolDatabase& db, const std::string& path, int line1,
                                 const ChainSegment& seg)
{
    if (!seg.isCall && seg.name == "this")
        return db.enclosingClassNameAt(path, line1);

    if (!seg.isCall && seg.name == "super") {
        std::string enclosing = db.enclosingClassNameAt(path, line1);
        if (enclosing.empty()) return "";
        auto rows = db.findSymbolsByName(enclosing);
        if (rows.empty()) return "";
        if (const SymbolRow* best = pickBestSymbol(rows, path); best->kind == "Class")
            return best->detail; // parent class name, "" if none (no extends)
        return "";
    }

    auto visible = db.findSymbolsVisibleAt(path, line1);
    const char* wantKind = seg.isCall ? "Function" : nullptr;
    for (auto& row : visible) {
        bool kindMatches = wantKind ? row.kind == wantKind
                                     : (row.kind == "Signal" || row.kind == "Parameter");
        if (kindMatches && row.name == seg.name)
            return peelDimensionLayers(row.detail, seg.indexDepth);
    }
    return "";
}

std::string resolveMemberSegment(SymbolDatabase& db, const std::string& curPath,
                                  const std::string& prevClass, const ChainSegment& seg)
{
    if (prevClass.empty()) return "";
    auto members = membersAcrossChain(db, prevClass, curPath);
    const char* wantKind = seg.isCall ? "Function" : nullptr;
    for (auto& row : members) {
        bool kindMatches = wantKind ? row.kind == wantKind
                                     : (row.kind == "Signal" || row.kind == "Parameter");
        if (kindMatches && row.name == seg.name)
            return peelDimensionLayers(row.detail, seg.indexDepth);
    }
    return "";
}

std::optional<std::string> resolveChain(SymbolDatabase& db, const std::string& path, int line1,
                                         const std::vector<ChainSegment>& segments)
{
    std::string current = resolveFirstSegment(db, path, line1, segments[0]);
    if (current.empty()) return std::nullopt;
    for (size_t i = 1; i < segments.size(); ++i) {
        current = resolveMemberSegment(db, path, current, segments[i]);
        if (current.empty()) return std::nullopt;
    }
    return current;
}

// True if the '(' at `parenPos` opens a named port connection ("
// .portName(" -- ubiquitous in real SV instantiations), i.e. an identifier
// immediately precedes it and a '.' that starts a fresh argument (preceded
// by '(' or ',', or the start of text) immediately precedes *that*. Without
// this check, a cursor positioned to type the connected signal (right after
// ".a(") would make findEnclosingParen stop at *this* paren instead of
// continuing out to the instantiation's own -- which is what active-
// parameter tracking below actually needs. Deliberately narrower than
// "any identifier followed by '('", so a genuine dotted method call like
// `obj.get_val(` -- where an identifier, not '(' or ',', precedes the '.'
// -- is correctly left alone (and simply fails to resolve later, since no
// per-parameter data exists for arbitrary calls; see this file's own header
// comment).
bool isNamedConnectionParen(const std::string& text, size_t parenPos)
{
    auto skipWsBack = [&](size_t& i) {
        while (i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) --i;
    };

    size_t i = parenPos;
    skipWsBack(i);
    size_t idEnd = i;
    while (i > 0 && isIdChar(text[i - 1])) --i;
    if (i == idEnd) return false; // no identifier immediately before '('

    skipWsBack(i);
    if (i == 0 || text[i - 1] != '.') return false;
    --i; // consume the '.'
    skipWsBack(i);
    return i == 0 || text[i - 1] == '(' || text[i - 1] == ',';
}

// The enclosing `(` of the argument list `offset` sits inside, found by
// walking backward with a paren-depth counter -- skipping past a named port
// connection's own parens (see isNamedConnectionParen) rather than stopping
// there, since those belong to the *same* argument list, not a nested one.
// A simple lexical scan -- deliberately not comment/string-aware (unlike
// findIdentifierOccurrences), since a comment or string literal inside a
// port-connection list is rare enough for this feature that the added
// complexity wasn't justified; see this file's own header comment for the
// feature's overall disclosed scope.
std::optional<size_t> findEnclosingParen(const std::string& text, size_t offset)
{
    int depth = 0;
    for (size_t i = offset; i-- > 0; ) {
        char c = text[i];
        if (c == ')') {
            ++depth;
        } else if (c == '(') {
            if (depth == 0) {
                if (isNamedConnectionParen(text, i)) continue;
                return i;
            }
            --depth;
        }
    }
    return std::nullopt;
}

// `text` with every comment and string-literal body replaced by spaces
// (newlines and the quotes themselves kept), so offsets are unchanged. All
// scanning runs on this copy: a comment between `)` and an instance name, or
// a '.'/'(' inside a string, can't derail it, and a cursor inside a comment
// or string resolves to nothing (handoff "wordAtPosition resolves symbols
// inside comments/strings").
namespace {

// One pass of the comment/string scanner over text[0, limit): blanks what it
// scans into `out` when given, and returns whether it ended inside a
// comment or string. A pair (`//`, `/*`, `*/`, an escape) straddling
// `limit` is scanned whole.
bool scanCommentsAndStrings(const std::string& text, size_t limit, std::string* out)
{
    enum { Code, Line, Block, Str } state = Code;
    auto blank = [&](size_t i) { if (out) (*out)[i] = ' '; };
    for (size_t i = 0; i < limit && i < text.size(); ++i) {
        const char c = text[i];
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';
        switch (state) {
        case Code:
            if (c == '/' && next == '/') { state = Line;  blank(i); blank(i + 1); ++i; }
            else if (c == '/' && next == '*') { state = Block; blank(i); blank(i + 1); ++i; }
            else if (c == '"') state = Str;
            break;
        case Line:
            if (c == '\n') state = Code; else blank(i);
            break;
        case Block:
            if (c == '*' && next == '/') { state = Code; blank(i); blank(i + 1); ++i; }
            else if (c != '\n') blank(i);
            break;
        case Str:
            if (c == '\\' && next != '\n' && next != '\0') { blank(i); blank(i + 1); ++i; }
            else if (c == '"' || c == '\n') state = Code;
            else blank(i);
            break;
        }
    }
    return state != Code;
}

} // namespace

std::string blankCommentsAndStrings(const std::string& text)
{
    std::string out = text;
    scanCommentsAndStrings(text, text.size(), &out);
    return out;
}

bool insideCommentOrString(const std::string& text, size_t offset)
{
    return scanCommentsAndStrings(text, offset, nullptr);
}

std::string symbolDoc(SymbolDatabase& db, const SymbolRow& row)
{
    std::string doc = db.docFor(row);
    // Only a class method has a prototype/body pair; at top level ("")
    // this would scan every top-level symbol.
    if (!doc.empty() || row.scope.empty() || (row.kind != "Function" && row.kind != "Task"))
        return doc;
    for (const auto& other : db.findSymbolsInScope(row.scope)) {
        if (other.name != row.name || other.kind != row.kind) continue;
        if (other.filePath == row.filePath && other.line == row.line && other.col == row.col)
            continue;
        if (doc = db.docFor(other); !doc.empty()) return doc;
    }
    return "";
}

std::string docMarkdown(const std::string& doc)
{
    std::string out;
    for (size_t i = 0; i < doc.size(); ++i) {
        if (doc[i] == '\n' && i > 0 && doc[i - 1] != '\n' && i + 1 < doc.size() &&
            doc[i + 1] != '\n')
            out += "  ";
        out += doc[i];
    }
    return out;
}
