#include "signature_help.h"
#include "lsp/symbol_utils.h"
#include "compiler/parse_record.h"
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

struct CallHeader {
    std::string name;
    // The immediate `Class::`/`pkg::` scope name directly before `name`
    // (plan.md §6.26), or "" for a genuinely unqualified call -- same
    // "segment directly before the last '::'" convention as
    // extractCalleeScope (src/compiler/sv_tree_walker.cpp), so
    // "T::type_id::create(" yields scope "type_id", not "T".
    std::string scope;
};

// Reads the single identifier immediately before `parenOffset`: a bare
// function/task call header, `<calleeName> (`, optionally preceded by a
// `Class::`/`pkg::` scope. Tried only after parseInstantiationHeader's
// two-identifier shape above fails to resolve to a known Module/Interface/
// Program -- see plan.md §6.22's follow-up section. Deliberately scoped to
// *undotted* calls only: if a '.' immediately precedes the identifier (or
// its scope prefix, skipping whitespace), this is a dotted call
// (`obj.method(`) that needs completion's own chain-resolution machinery,
// not this lexical scan -- fails closed (nullopt) rather than guessing.
std::optional<CallHeader> parseCallHeader(const std::string& text, size_t parenOffset)
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
    std::string name = readIdentBack(i);
    if (name.empty()) return std::nullopt;

    // A "::" immediately before the name means a scope-qualified call --
    // walk back over one or more "Scope::" segments the same way
    // extractCalleeScope's "last segment before the last '::'" rule does,
    // keeping only the immediate one.
    std::string scope;
    while (i >= 2 && text[i - 1] == ':' && text[i - 2] == ':') {
        i -= 2;
        std::string seg = readIdentBack(i);
        if (seg.empty()) return std::nullopt;
        if (scope.empty()) scope = seg; // keep only the immediate (last) segment
    }

    skipWsBack(i);
    if (i > 0 && text[i - 1] == '.') return std::nullopt; // dotted -- see parseDottedCallHeader

    return CallHeader{std::move(name), std::move(scope)};
}

struct DottedCall {
    std::string calleeName;
    SymbolRow   symbol;
};

// Resolves a dotted call (`obj.method(`, or a longer chain
// `obj.field.method(`) -- tried only after parseCallHeader above fails,
// i.e. only when a '.' (not '::') immediately precedes the call name
// (plan.md §6.27). Reuses dot-completion's own chain-resolution machinery
// (dotCompletionContext + resolveChain, lsp/symbol_utils.h) rather than
// re-implementing chain parsing here: calling dotCompletionContext with the
// cursor positioned right at the call name's own end (not the user's actual
// cursor, which may be deep inside a multi-line argument list) makes it
// return exactly the receiver chain as `segments` and the call's own name
// as `prefix` -- precisely what's needed, for free. `resolveChain` then
// resolves the receiver chain to its declared type, and
// SymbolDatabase::resolveMethod (plan.md §6.26) finds `prefix` as a
// Function/Task on that type or one of its ancestors via `extends`, the
// same inheritance-aware resolution the bare-call and `Class::`-qualified
// paths above already use. Returns nullopt (fail closed) if the receiver
// chain or the method itself doesn't resolve -- an undeclared receiver, an
// unresolvable intermediate segment, or a genuinely unknown method.
std::optional<DottedCall> parseDottedCallHeader(
    const std::string& text, size_t parenOffset, SymbolDatabase& db, const std::string& curPath)
{
    size_t nameEnd = parenOffset;
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(text[nameEnd - 1]))) --nameEnd;

    const lsp::Position pos = positionForOffset(text, nameEnd);
    auto dot = dotCompletionContext(text, pos.line, pos.character);
    if (!dot) return std::nullopt;

    const int line1 = static_cast<int>(pos.line) + 1;
    auto receiverType = resolveChain(db, curPath, line1, dot->segments);
    if (!receiverType) return std::nullopt;

    auto method = db.resolveMethod(*receiverType, dot->prefix, curPath);
    if (!method) return std::nullopt;

    return DottedCall{dot->prefix, *method};
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
// findEnclosingParen, lsp/symbol_utils.h).
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

// A function/task parameter's detail carries a "<direction> <type>" prefix
// and, if a default value is present, a "= <value>" suffix past
// PARAM_DEFAULT_VALUE_SEP (see enterTf_port_item, sv_tree_walker.cpp) --
// rendered as "<prefix> <name> <suffix>", e.g. "int width = 8". A plain
// module/interface/program port's detail never contains the separator, so
// it falls through to the original "<direction> <name>" shape unchanged.
std::string portLabel(const SymbolRow& port)
{
    auto sep = port.detail.find(PARAM_DEFAULT_VALUE_SEP);
    const std::string prefix = sep == std::string::npos ? port.detail : port.detail.substr(0, sep);
    const std::string suffix = sep == std::string::npos ? "" : port.detail.substr(sep + 1);
    std::string label = prefix.empty() ? port.name : prefix + " " + port.name;
    label += suffix;
    return label;
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

    const std::string curPath{params.textDocument.uri.path()};
    const int line1 = static_cast<int>(params.position.line) + 1;

    // Try the module/interface/program instantiation shape first
    // (`<TypeName> <InstanceName> (`); only if that fails to resolve to a
    // known design-unit type, try the bare function/task call shape
    // (`<calleeName> (`, plan.md §6.22 follow-up) instead.
    std::string calleeName;
    std::vector<SymbolRow> typeRows;
    std::optional<SymbolRow> resolvedCallee;
    if (auto header = parseInstantiationHeader(docText, *parenOffset)) {
        for (auto& row : db.findSymbolsByName(header->typeName))
            if (row.kind == "Module" || row.kind == "Interface" || row.kind == "Program")
                typeRows.push_back(row);
        calleeName = header->typeName;
    }
    if (typeRows.empty()) {
        if (auto callee = parseCallHeader(docText, *parenOffset)) {
            calleeName = callee->name;
            if (!callee->scope.empty()) {
                // Explicitly `Class::`/`pkg::`-qualified -- resolve
                // strictly within that name's own class hierarchy, and fail
                // closed if it isn't a known class or doesn't declare this
                // method anywhere in it (plan.md §6.26). Never falls back
                // to a flat whole-database search for this case -- that
                // fallback is what made `type_id::get()`-style calls
                // resolve against an unrelated same-named method elsewhere.
                resolvedCallee = db.resolveMethod(callee->scope, callee->name, curPath);
            } else {
                // Unqualified -- try the call site's own enclosing class
                // hierarchy first (an inherited method called bare), then
                // fall back to the pre-existing flat search only when
                // there's no class context at all to have gotten wrong.
                std::string enclosing = db.enclosingClassNameAt(curPath, line1);
                if (!enclosing.empty())
                    resolvedCallee = db.resolveMethod(enclosing, callee->name, curPath);
                if (!resolvedCallee) {
                    for (auto& row : db.findSymbolsByName(callee->name))
                        if (row.kind == "Function" || row.kind == "Task")
                            typeRows.push_back(row);
                }
            }
        } else if (auto dotted = parseDottedCallHeader(docText, *parenOffset, db, curPath)) {
            // A '.' (not '::') precedes the call name -- plan.md §6.27.
            calleeName = dotted->calleeName;
            resolvedCallee = dotted->symbol;
        }
    }
    if (!resolvedCallee && typeRows.empty()) return nullptr; // fail closed

    const SymbolRow best = resolvedCallee ? *resolvedCallee : *pickBestSymbol(typeRows, curPath);
    const std::string scope =
        best.scope.empty() ? best.name : best.scope + "::" + best.name;

    const std::vector<SymbolRow> ports = db.portsOf(scope);

    const auto active = computeActiveParam(docText, *parenOffset, *cursorOffset);

    lsp::SignatureInformation sig;
    std::string label = calleeName + "(";
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
