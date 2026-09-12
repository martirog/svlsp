#include "signature_help.h"
#include "lsp/symbol_utils.h"
#include <algorithm>
#include <cctype>
#include <optional>

namespace {

bool isIdentChar(unsigned char c)
{
    return std::isalnum(c) || c == '_' || c == '$';
}

// Converts a 0-based (line, character) LSP position to a flat byte offset
// into `text`. Returns std::nullopt if `line` is beyond the text.
std::optional<size_t> toOffset(const std::string& text, unsigned line, unsigned character)
{
    size_t offset = 0;
    unsigned curLine = 0;
    while (curLine < line) {
        size_t nl = text.find('\n', offset);
        if (nl == std::string::npos) return std::nullopt;
        offset = nl + 1;
        ++curLine;
    }
    offset += character;
    return offset <= text.size() ? std::optional<size_t>(offset) : std::nullopt;
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
    while (i > 0 && isIdentChar(static_cast<unsigned char>(text[i - 1]))) --i;
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

struct InstantiationHeader {
    std::string typeName;
    std::string instanceName;
};

// Reads the two identifiers immediately before `parenOffset` (the `(`
// itself): `<typeName> <instanceName> (`. Fails (returns nullopt) for
// anything else, including a parameter override block (`Module #(...)
// inst (`) -- a disclosed, un-handled shape; see this file's own header.
std::optional<InstantiationHeader> parseInstantiationHeader(
    const std::string& text, size_t parenOffset)
{
    auto skipWsBack = [&](size_t& i) {
        while (i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) --i;
    };
    auto readIdentBack = [&](size_t& i) {
        size_t end = i;
        while (i > 0 && isIdentChar(static_cast<unsigned char>(text[i - 1]))) --i;
        return text.substr(i, end - i);
    };

    size_t i = parenOffset;
    skipWsBack(i);
    std::string instanceName = readIdentBack(i);
    if (instanceName.empty()) return std::nullopt;

    skipWsBack(i);
    std::string typeName = readIdentBack(i);
    if (typeName.empty()) return std::nullopt;

    return InstantiationHeader{std::move(typeName), std::move(instanceName)};
}

struct ActiveParam {
    int                        index;
    std::optional<std::string> namedPort;
};

// Scans forward from just inside `parenOffset` to `cursorOffset`, counting
// top-level (paren/bracket/brace-depth-0) commas to get a positional index,
// and separately checking whether the argument the cursor is currently in
// starts with a named port connection (`.portName(` -- ubiquitous in real
// SV verification code) so that can be resolved by name instead of
// position. No string-literal awareness (same disclosed simplification as
// findEnclosingParen above).
ActiveParam computeActiveParam(const std::string& text, size_t parenOffset, size_t cursorOffset)
{
    int index = 0;
    int depth = 0;
    size_t segStart = parenOffset + 1;
    for (size_t i = parenOffset + 1; i < cursorOffset && i < text.size(); ++i) {
        char c = text[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == ',' && depth == 0) { ++index; segStart = i + 1; }
    }

    size_t j = segStart;
    while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
    if (j < text.size() && text[j] == '.') {
        ++j;
        size_t idStart = j;
        while (j < text.size() && isIdentChar(static_cast<unsigned char>(text[j]))) ++j;
        if (j > idStart) return {index, text.substr(idStart, j - idStart)};
    }
    return {index, std::nullopt};
}

std::string portLabel(const SymbolRow& port)
{
    return port.detail.empty() ? port.name : port.detail + " " + port.name;
}

} // namespace

lsp::TextDocument_SignatureHelpResult SignatureHelpProvider::getSignatureHelp(
    const lsp::SignatureHelpParams& params, SymbolDatabase& db,
    const std::string& docText)
{
    auto cursorOffset = toOffset(docText, params.position.line, params.position.character);
    if (!cursorOffset) return nullptr;

    auto parenOffset = findEnclosingParen(docText, *cursorOffset);
    if (!parenOffset) return nullptr;

    auto header = parseInstantiationHeader(docText, *parenOffset);
    if (!header) return nullptr;

    auto candidates = db.findSymbolsByName(header->typeName);
    std::vector<SymbolRow> typeRows;
    for (auto& row : candidates) {
        if (row.kind == "Module" || row.kind == "Interface" || row.kind == "Program")
            typeRows.push_back(row);
    }
    if (typeRows.empty()) return nullptr; // not a known design-unit type -- fail closed

    const std::string curPath{params.textDocument.uri.path()};
    const SymbolRow* best = pickBestSymbol(typeRows, curPath);
    const std::string scope =
        best->scope.empty() ? best->name : best->scope + "::" + best->name;

    std::vector<SymbolRow> ports;
    for (auto& row : db.findSymbolsInScope(scope))
        if (row.kind == "Port") ports.push_back(row);
    std::sort(ports.begin(), ports.end(), [](const SymbolRow& a, const SymbolRow& b) {
        return a.line != b.line ? a.line < b.line : a.col < b.col;
    });

    const auto active = computeActiveParam(docText, *parenOffset, *cursorOffset);

    lsp::SignatureInformation sig;
    std::string label = header->typeName + "(";
    lsp::Array<lsp::ParameterInformation> params_;
    int activeIndex = active.index;
    if (active.namedPort) {
        auto it = std::find_if(ports.begin(), ports.end(),
            [&](const SymbolRow& p) { return p.name == *active.namedPort; });
        if (it != ports.end())
            activeIndex = static_cast<int>(it - ports.begin());
    }
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (i > 0) label += ", ";
        const std::string plabel = portLabel(ports[i]);
        label += plabel;
        lsp::ParameterInformation p;
        p.label = plabel;
        params_.push_back(std::move(p));
    }
    label += ")";

    sig.label      = std::move(label);
    sig.parameters = std::move(params_);
    if (!ports.empty() && activeIndex >= 0 &&
        static_cast<std::size_t>(activeIndex) < ports.size()) {
        sig.activeParameter = static_cast<unsigned>(activeIndex);
    }

    lsp::SignatureHelp help;
    help.signatures.push_back(std::move(sig));
    help.activeSignature = 0u;
    return help;
}
