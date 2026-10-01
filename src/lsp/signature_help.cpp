#include "signature_help.h"
#include "lsp/symbol_utils.h"
#include "compiler/parse_record.h"
#include "lsp/macro_resolution.h"
#include "lsp/symbol_resolution.h"
#include "lsp/sv_keyword_signatures.h"
#include "lsp/sv_system_tasks.h"
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
    // True for a `Class::`/`pkg::`-qualified call (plan.md §6.26); the
    // qualifier itself is resolved by resolveSymbolAt.
    bool qualified = false;
};

// Reads the single identifier immediately before `parenOffset`: a bare
// function/task call header, `<calleeName> (`, optionally preceded by a
// `Class::`/`pkg::` scope. Tried only after parseInstantiationHeader's
// two-identifier shape above fails to resolve to a known Module/Interface/
// Program -- see plan.md §6.22's follow-up section. Deliberately scoped to
// *undotted* calls only: if a '.' immediately precedes the identifier (or
// its scope prefix, skipping whitespace), this is a dotted call
// (`obj.method(`, see dottedCallName) -- nullopt.
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
    // walk back over one or more "Scope::" segments.
    bool qualified = false;
    while (i >= 2 && text[i - 1] == ':' && text[i - 2] == ':') {
        i -= 2;
        if (readIdentBack(i).empty()) return std::nullopt;
        qualified = true;
    }

    skipWsBack(i);
    if (i > 0 && text[i - 1] == '.') return std::nullopt; // dotted -- see dottedCallName

    return CallHeader{std::move(name), qualified};
}

// The call name ending at `nameEnd` resolved as hover/definition resolve it
// (resolveSymbolAt: `::` qualifiers, `.` receiver chains, bare names
// innermost-scope-first through imports), kept only if that is exact and a
// Function/Task. A qualifier or receiver that is understood but has no such
// method, or isn't understood at all, gives nullopt (plan.md §6.26's
// fail-closed rule).
std::optional<SymbolRow> resolveCallName(SymbolDatabase& db, const std::string& curPath,
                                         const std::string& text, size_t nameEnd,
                                         const std::string& name)
{
    const lsp::Position pos = positionForOffset(text, nameEnd - name.size());
    auto r = resolveSymbolAt(db, curPath, text, pos.line, pos.character);
    if (!r || !r->exact || (r->row.kind != "Function" && r->row.kind != "Task"))
        return std::nullopt;
    return r->row;
}

// The name of a dotted call (`obj.method(`, `obj.field.method(`) ending at
// `nameEnd` -- tried only after parseCallHeader fails, i.e. only when a '.'
// (not '::') precedes it (plan.md §6.27). dotCompletionContext at the name's
// own end returns the call name as `prefix`.
std::optional<std::string> dottedCallName(const std::string& text, size_t nameEnd)
{
    const lsp::Position pos = positionForOffset(text, nameEnd);
    auto dot = dotCompletionContext(text, pos.line, pos.character);
    if (!dot || dot->prefix.empty()) return std::nullopt;
    return dot->prefix;
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

// A system task/function call (`$display(`, plan.md §6.29 part C), from the
// static table in sv_system_tasks.h. activeParameter clamps to a variadic
// tail (`args...`) once past the fixed parameters. `$fatal([finish_number],
// format, ...)`'s leading optional parameter is taken as omitted when the
// first argument is a string literal, so the format string is highlighted.
lsp::SignatureHelp systemTaskHelp(const SystemTask& task, const std::string& text,
                                  size_t parenOffset, size_t cursorOffset)
{
    const std::vector<std::string> params = systemTaskParams(task);
    int index = computeActiveParam(text, parenOffset, cursorOffset).index;

    if (params.size() > 1 && params[0].front() == '[' && params[1] == "format") {
        size_t j = parenOffset + 1;
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        if (j < text.size() && text[j] == '"') ++index;
    }
    const auto isVariadic = [](const std::string& p) {
        return p.ends_with("...") || p.ends_with("...]");
    };
    if (!params.empty() && index >= static_cast<int>(params.size()) && isVariadic(params.back()))
        index = static_cast<int>(params.size()) - 1;

    lsp::SignatureInformation sig;
    std::string label = std::string(task.name) + "(";
    lsp::Array<lsp::ParameterInformation> params_;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) label += ", ";
        label += params[i];
        lsp::ParameterInformation p;
        p.label = params[i];
        params_.push_back(std::move(p));
    }
    label += ")";

    sig.label         = std::move(label);
    sig.documentation = std::string(task.doc);
    sig.parameters    = std::move(params_);
    if (index >= 0 && static_cast<std::size_t>(index) < params.size())
        sig.activeParameter = static_cast<unsigned>(index);

    lsp::SignatureHelp help;
    help.signatures.push_back(std::move(sig));
    help.activeSignature = 0u;
    return help;
}

// A keyword construct's header (`for (`, `assert property (`, plan.md §6.29
// part B): the identifier directly before `(` is a KEYWORD_SIGNATURES
// keyword, and for an entry with a prefix, the identifier before it is that
// prefix. An entry with a matching prefix wins over one without. (A
// backtick call, `` `assert( ``, never gets here: macros are checked first.)
const KeywordSignature* findKeywordHeader(const std::string& text, size_t parenOffset)
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
    const std::string keyword = readIdentBack(i);
    if (keyword.empty()) return nullptr;
    skipWsBack(i);
    const std::string before = readIdentBack(i);

    const KeywordSignature* unprefixed = nullptr;
    for (const auto& k : KEYWORD_SIGNATURES) {
        if (k.keyword != keyword) continue;
        if (k.prefix.empty()) unprefixed = &k;
        else if (k.prefix == before) return &k;
    }
    return unprefixed;
}

lsp::SignatureHelp keywordHelp(const KeywordSignature& kw, const std::string& text,
                               size_t parenOffset, size_t cursorOffset)
{
    std::vector<std::string> params;
    for (size_t start = 0;;) {
        size_t sep = kw.params.find("; ", start);
        params.emplace_back(kw.params.substr(
            start, sep == std::string_view::npos ? std::string_view::npos : sep - start));
        if (sep == std::string_view::npos) break;
        start = sep + 2;
    }

    // Top-level `;` count (for) or whether a top-level `[` was typed (foreach).
    int index = 0;
    int depth = 0;
    for (size_t i = parenOffset + 1; i < cursorOffset && i < text.size(); ++i) {
        const char c = text[i];
        if (c == '[' && depth == 0 && kw.separator == KeywordSeparator::Bracket) index = 1;
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if (c == ';' && depth == 0 && kw.separator == KeywordSeparator::Semicolon) ++index;
    }

    std::string head = kw.prefix.empty() ? std::string(kw.keyword)
                                         : std::string(kw.prefix) + " " + std::string(kw.keyword);
    std::string label;
    switch (kw.separator) {
    case KeywordSeparator::Bracket:
        label = head + " (" + params[0] + "[" + params[1] + "])";
        break;
    case KeywordSeparator::Comma:
        label = head + "(" + params[0] + ")";
        break;
    default:
        label = head + " (";
        for (size_t i = 0; i < params.size(); ++i) label += (i ? "; " : "") + params[i];
        label += ")";
    }

    lsp::SignatureInformation sig;
    lsp::Array<lsp::ParameterInformation> params_;
    for (auto& p : params) {
        lsp::ParameterInformation pi;
        pi.label = p;
        params_.push_back(std::move(pi));
    }
    sig.label         = std::move(label);
    sig.documentation = std::string(kw.doc);
    sig.parameters    = std::move(params_);
    if (index < static_cast<int>(params.size())) sig.activeParameter = static_cast<unsigned>(index);

    lsp::SignatureHelp help;
    help.signatures.push_back(std::move(sig));
    help.activeSignature = 0u;
    return help;
}

// A macro invocation's name (plan.md §6.29 part A): the identifier directly
// before `(`, itself directly preceded by a backtick. Empty if it isn't one.
std::string macroCallName(const std::string& text, size_t parenOffset)
{
    size_t i = parenOffset;
    while (i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) --i;
    const size_t end = i;
    while (i > 0 && isIdentChar(static_cast<unsigned char>(text[i - 1]))) --i;
    if (i == end || i == 0 || text[i - 1] != '`') return {};
    return text.substr(i, end - i);
}

lsp::SignatureHelp macroHelp(const MacroRow& macro, const std::string& text,
                             size_t parenOffset, size_t cursorOffset)
{
    const int index = computeActiveParam(text, parenOffset, cursorOffset).index;

    lsp::SignatureInformation sig;
    lsp::Array<lsp::ParameterInformation> params_;
    for (std::size_t i = 0; i < macro.params.size(); ++i) {
        std::string plabel = macro.params[i];
        if (i < macro.defaults.size() && macro.defaults[i]) plabel += " = " + *macro.defaults[i];
        lsp::ParameterInformation p;
        p.label = plabel;
        params_.push_back(std::move(p));
    }

    sig.label      = macroSignature(macro);
    sig.parameters = std::move(params_);
    if (!macro.doc.empty()) sig.documentation = macro.doc;
    if (index >= 0 && static_cast<std::size_t>(index) < macro.params.size())
        sig.activeParameter = static_cast<unsigned>(index);

    lsp::SignatureHelp help;
    help.signatures.push_back(std::move(sig));
    help.activeSignature = 0u;
    return help;
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

    // A macro invocation -- checked first, and never falls through: a
    // backtick call is never a function, task or keyword. An unknown or
    // object-like macro gets nothing.
    const std::string macroName = macroCallName(docText, *parenOffset);
    if (!macroName.empty()) {
        auto macro = pickMacro(db.findMacros(macroName), curPath, line1);
        if (!macro || !macro->isFunctionLike) return nullptr;
        return macroHelp(*macro, docText, *parenOffset, *cursorOffset);
    }

    // A keyword construct's header -- a keyword is never a user-declared
    // name.
    if (const KeywordSignature* kw = findKeywordHeader(docText, *parenOffset))
        return keywordHelp(*kw, docText, *parenOffset, *cursorOffset);

    // Try the module/interface/program instantiation shape first
    // (`<TypeName> <InstanceName> (`); only if that fails to resolve to a
    // known design-unit type, try the bare function/task call shape
    // (`<calleeName> (`, plan.md §6.22 follow-up) instead.
    std::string calleeName;
    std::vector<SymbolRow> typeRows;
    std::optional<SymbolRow> resolvedCallee;
    std::string systemTaskName; // an unqualified `$name(` call
    // `new(` -- a constructor, found by context (the assigned variable's
    // class, `super.`, `C::`) by the resolver; never by name alone.
    size_t nameEnd = *parenOffset;
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(docText[nameEnd - 1]))) --nameEnd;
    if (nameEnd >= 3 && docText.compare(nameEnd - 3, 3, "new") == 0 &&
        (nameEnd == 3 || !isIdentChar(static_cast<unsigned char>(docText[nameEnd - 4])))) {
        const lsp::Position pos = positionForOffset(docText, nameEnd - 3);
        auto ctor = resolveSymbolAt(db, curPath, docText, pos.line, pos.character);
        if (!ctor || ctor->row.name != "new") return nullptr;
        calleeName     = "new";
        resolvedCallee = ctor->row;
    } else if (auto header = parseInstantiationHeader(docText, *parenOffset)) {
        for (auto& row : db.findSymbolsByName(header->typeName))
            if (row.kind == "Module" || row.kind == "Interface" || row.kind == "Program")
                typeRows.push_back(row);
        calleeName = header->typeName;
    }
    if (typeRows.empty() && !resolvedCallee) {
        if (auto callee = parseCallHeader(docText, *parenOffset)) {
            calleeName = callee->name;
            if (!callee->qualified && calleeName.front() == '$') {
                systemTaskName = calleeName;
            } else {
                resolvedCallee = resolveCallName(db, curPath, docText, nameEnd, calleeName);
            }
            // Only an unqualified call that nothing visible declares falls
            // back to a name-only search; a `Class::`/`pkg::`-qualified one
            // fails closed (plan.md §6.26) -- that fallback is what made
            // `type_id::get()`-style calls resolve against an unrelated
            // same-named method elsewhere.
            if (!resolvedCallee && !callee->qualified) {
                for (auto& row : db.findSymbolsByName(callee->name))
                    if (row.kind == "Function" || row.kind == "Task")
                        typeRows.push_back(row);
            }
        } else if (auto dotted = dottedCallName(docText, nameEnd)) {
            calleeName     = *dotted;
            resolvedCallee = resolveCallName(db, curPath, docText, nameEnd, calleeName);
        }
    }
    if (!resolvedCallee && typeRows.empty()) {
        // Built-in system task/function -- only after the DB lookup fails.
        if (const SystemTask* task = findSystemTask(systemTaskName))
            return systemTaskHelp(*task, docText, *parenOffset, *cursorOffset);
        return nullptr; // fail closed
    }

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
        if (std::string doc = db.docFor(ports[i]); !doc.empty()) p.documentation = std::move(doc);
        params_.push_back(std::move(p));
    }
    label += ")";

    sig.label      = std::move(label);
    sig.parameters = std::move(params_);
    if (std::string doc = symbolDoc(db, best); !doc.empty()) sig.documentation = std::move(doc);
    if (!ports.empty() && activeIndex >= 0 &&
        static_cast<std::size_t>(activeIndex) < ports.size()) {
        sig.activeParameter = static_cast<unsigned>(activeIndex);
    }

    lsp::SignatureHelp help;
    help.signatures.push_back(std::move(sig));
    help.activeSignature = 0u;
    return help;
}
