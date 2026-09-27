#include "lsp/symbol_resolution.h"
#include "lsp/symbol_utils.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>
#include <vector>

namespace {

// Recursion guard: resolving a named connection's owner (the instantiated
// module, or the called function) re-enters resolution at another position.
constexpr int kMaxDepth = 4;

bool isIdChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

std::string qualifiedScopeOf(const SymbolRow& r)
{
    return r.scope.empty() ? r.name : r.scope + "::" + r.name;
}

bool isCallable(const SymbolRow& r) { return r.kind == "Function" || r.kind == "Task"; }
bool isDesignUnit(const SymbolRow& r)
{
    return r.kind == "Module" || r.kind == "Interface" || r.kind == "Program";
}

std::optional<size_t> offsetOf(const std::string& text, unsigned line, unsigned character)
{
    size_t offset = 0;
    for (unsigned l = 0; l < line; ++l) {
        size_t nl = text.find('\n', offset);
        if (nl == std::string::npos) return std::nullopt;
        offset = nl + 1;
    }
    size_t lineEnd = text.find('\n', offset);
    if (lineEnd == std::string::npos) lineEnd = text.size();
    if (offset + character > lineEnd) return std::nullopt;
    return offset + character;
}

int line1At(const std::string& text, size_t offset)
{
    return static_cast<int>(std::count(text.begin(), text.begin() + offset, '\n')) + 1;
}

// Moves `i` left past whitespace (newlines included).
void skipWsBack(const std::string& text, size_t& i)
{
    while (i > 0 && std::isspace(static_cast<unsigned char>(text[i - 1]))) --i;
}

size_t skipWsFwd(const std::string& text, size_t i)
{
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    return i;
}

// Reads the identifier ending at `end` (exclusive); returns its start, or
// `end` when there is none.
size_t identStartBefore(const std::string& text, size_t end)
{
    size_t i = end;
    while (i > 0 && isIdChar(text[i - 1])) --i;
    return i;
}

// The '(' matching the ')' at `close`, walking left; npos if unbalanced.
size_t matchingOpenParen(const std::string& text, size_t close)
{
    int depth = 0;
    for (size_t i = close + 1; i-- > 0; ) {
        if (text[i] == ')') ++depth;
        else if (text[i] == '(' && --depth == 0) return i;
    }
    return std::string::npos;
}

// True if the two characters before `i` are a `::` scope operator.
bool scopeOpBefore(const std::string& text, size_t i)
{
    return i >= 2 && text[i - 1] == ':' && text[i - 2] == ':' && (i < 3 || text[i - 3] != ':');
}

enum class KindFilter { Any, ScopeLike, Callable, Value, Typedef };

bool kindMatches(const SymbolRow& r, KindFilter f)
{
    switch (f) {
    case KindFilter::Any:       return true;
    case KindFilter::ScopeLike: return r.kind == "Package" || r.kind == "Class";
    case KindFilter::Callable:  return isCallable(r);
    case KindFilter::Value:     return r.kind == "Signal" || r.kind == "Parameter" || r.kind == "Port";
    case KindFilter::Typedef:   return r.kind == "Typedef";
    }
    return true;
}

// Earliest-declared row, so a stable choice among same-scope duplicates.
const SymbolRow* earliest(const std::vector<const SymbolRow*>& rows)
{
    const SymbolRow* best = nullptr;
    for (auto* r : rows)
        if (!best || r->line < best->line || (r->line == best->line && r->col < best->col))
            best = r;
    return best;
}

// The last of `rows` declared at or before `line1`, else the earliest: the
// nearest preceding declaration in a non-class scope -- an out-of-class
// body's own argument rather than the extern prototype's same-named one,
// both in scope `C::m` (plan.md §6.30 step D).
const SymbolRow* closestBefore(const std::vector<const SymbolRow*>& rows, int line1)
{
    const SymbolRow* best = nullptr;
    for (auto* r : rows)
        if (r->line <= line1 && (!best || r->line > best->line ||
                                 (r->line == best->line && r->col > best->col)))
            best = r;
    return best ? best : earliest(rows);
}

class Resolver {
public:
    Resolver(SymbolDatabase& db, const std::string& path, const std::string& text)
        : m_db(db), m_path(path), m_text(blankCommentsAndStrings(text)) {}

    std::optional<ResolvedSymbol> resolveAt(size_t offset, int depth);
    int64_t overrideFamilyId(const SymbolRow& row) const;

private:
    SymbolDatabase&    m_db;
    const std::string& m_path;
    const std::string  m_text; // comments/strings blanked, same offsets
    mutable int        m_typeDepth = 0; // guards resolveTypeName's own recursion

    std::optional<ResolvedSymbol> fallback(const std::string& name) const;

    std::vector<std::string> scopeChain(const std::string& path, int line1) const;
    std::optional<SymbolRow> classRowForScope(const std::string& scope) const;
    std::vector<SymbolRow> classChainRows(const SymbolRow& cls) const;
    std::optional<SymbolRow> lookupBare(const std::string& path, int line1,
                                        const std::string& name, KindFilter filter,
                                        bool withInherited = true) const;
    std::optional<SymbolRow> resolveTypeName(const std::string& type, const std::string& ctxPath,
                                             int ctxLine1) const;
    std::optional<std::string> resolveQualifierScope(const std::vector<std::string>& segs,
                                                     const std::string& ctxPath,
                                                     int ctxLine1) const;
    std::optional<SymbolRow> memberInScope(const std::string& scope, const std::string& name,
                                           KindFilter prefer) const;
    std::optional<SymbolRow> typeOfRow(const SymbolRow& row, int indexDepth) const;
    std::optional<SymbolRow> enclosingClassRow(int line1) const;
    std::optional<SymbolRow> resolveReceiver(const std::vector<ChainSegment>& segs,
                                             int line1) const;
    std::optional<SymbolRow> constructorIn(const std::string& classScope) const;
    std::optional<ResolvedSymbol> resolveNew(size_t start, size_t before, int line1) const;
    struct Owner { SymbolRow row; bool paramList; };
    std::optional<Owner> connectionOwner(size_t paren, int depth);
};

std::optional<ResolvedSymbol> Resolver::fallback(const std::string& name) const
{
    auto rows = m_db.findSymbolsByName(name);
    if (rows.empty()) return std::nullopt;
    return ResolvedSymbol{*pickBestSymbol(rows, m_path), false};
}

// Same chain findSymbolsVisibleAt builds: innermost scope first, ending "".
std::vector<std::string> Resolver::scopeChain(const std::string& path, int line1) const
{
    std::vector<std::string> chain;
    std::string cur = m_db.scopeAtPosition(path, line1);
    while (true) {
        chain.push_back(cur);
        auto sep = cur.rfind("::");
        if (sep == std::string::npos) break;
        cur = cur.substr(0, sep);
    }
    if (chain.back() != "") chain.push_back("");
    return chain;
}

std::optional<SymbolRow> Resolver::classRowForScope(const std::string& scope) const
{
    if (scope.empty()) return std::nullopt;
    auto sep = scope.rfind("::");
    const std::string name = sep == std::string::npos ? scope : scope.substr(sep + 2);
    for (auto& r : m_db.findSymbolsByName(name))
        if (r.kind == "Class" && qualifiedScopeOf(r) == scope) return r;
    return std::nullopt;
}

// `cls` followed by each `extends` ancestor, each parent resolved in the
// context of the class that names it. Stops at an unresolvable parent or a
// cycle.
std::vector<SymbolRow> Resolver::classChainRows(const SymbolRow& cls) const
{
    std::vector<SymbolRow> chain{cls};
    std::set<std::string> seen{qualifiedScopeOf(cls)};
    while (!chain.back().detail.empty()) {
        auto parent = resolveTypeName(chain.back().detail, chain.back().filePath, chain.back().line);
        if (!parent || !seen.insert(qualifiedScopeOf(*parent)).second) break;
        chain.push_back(*parent);
    }
    return chain;
}

// The id of the topmost ancestor's same-named Function/Task for a class
// method (the root of its override family), else `row.id`.
int64_t Resolver::overrideFamilyId(const SymbolRow& row) const
{
    if (!isCallable(row)) return row.id;
    // Constructors are never inherited or overridden: each class's own `new`
    // (its extern prototype and out-of-class body alike) is one symbol.
    if (row.name == "new") {
        auto own = constructorIn(row.scope);
        return own ? own->id : row.id;
    }
    auto cls = classRowForScope(row.scope);
    if (!cls) return row.id;
    int64_t root = row.id;
    const auto chain = classChainRows(*cls);
    for (size_t i = 1; i < chain.size(); ++i)
        for (const auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(chain[i])))
            if (r.name == row.name && isCallable(r)) {
                root = r.id;
                break;
            }
    return root;
}

std::optional<SymbolRow> Resolver::lookupBare(const std::string& path, int line1,
                                              const std::string& name, KindFilter filter,
                                              bool withInherited) const
{
    const auto visible = m_db.findSymbolsVisibleAt(path, line1);
    const auto chain   = scopeChain(path, line1);

    // 1. The lexical scope chain, innermost first. At a class's own level,
    //    its inherited members come before anything outside the class.
    for (const auto& scope : chain) {
        std::vector<const SymbolRow*> hits;
        for (const auto& r : visible)
            if (r.filePath == path && r.scope == scope && r.name == name && kindMatches(r, filter))
                hits.push_back(&r);
        auto cls = classRowForScope(scope);
        // Class members are order-independent, and a method's extern
        // prototype (earliest) must win over its out-of-class body so every
        // call resolves to the same row; elsewhere the nearest preceding
        // declaration wins.
        if (!hits.empty()) return cls ? *earliest(hits) : *closestBefore(hits, line1);
        if (!cls) continue;
        // The class's own members from any file: an out-of-class body
        // (plan.md §6.30 step D) may live in a different file than its class,
        // and the visible set's local chain only covers the cursor's file.
        {
            std::vector<SymbolRow> own;
            for (auto& r : m_db.findSymbolsInScope(scope))
                if (r.name == name && kindMatches(r, filter)) own.push_back(r);
            std::vector<const SymbolRow*> ptrs;
            for (auto& r : own) ptrs.push_back(&r);
            if (!ptrs.empty()) return *earliest(ptrs);
        }
        if (!withInherited) continue;
        auto ancestors = classChainRows(*cls);
        for (size_t i = 1; i < ancestors.size(); ++i)
            for (auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(ancestors[i])))
                if (r.name == name && kindMatches(r, filter)) return r;
    }

    auto isLocal = [&](const SymbolRow& r) {
        return r.filePath == path && std::find(chain.begin(), chain.end(), r.scope) != chain.end();
    };

    // 2. A specific import (`import pkg::name;`).
    const auto imports = m_db.importsForFile(path);
    for (const auto& r : visible) {
        if (isLocal(r) || r.name != name || !kindMatches(r, filter)) continue;
        for (const auto& imp : imports)
            if (imp.item == name && imp.pkgName == r.scope) return r;
    }

    // 3. Wildcard-imported (or re-exported) package members.
    for (const auto& r : visible)
        if (!isLocal(r) && !r.scope.empty() && r.name == name && kindMatches(r, filter)) return r;

    // 4. Other files' top level.
    std::vector<SymbolRow> top;
    for (const auto& r : visible)
        if (!isLocal(r) && r.scope.empty() && r.name == name && kindMatches(r, filter))
            top.push_back(r);
    if (!top.empty()) return *pickBestSymbol(top, path);
    return std::nullopt;
}

// Resolves a declared type name (a `detail` value, possibly
// `pkg::`/`Outer::`/`$unit::`-qualified) to its Class row, as seen from
// (ctxPath, ctxLine1) -- the declaration that wrote it. A Typedef is
// followed to the type it aliases, resolved where the typedef is declared.
// A bare name nothing visible declares still resolves if exactly one class
// (or else exactly one typedef) has that name.
std::optional<SymbolRow> Resolver::resolveTypeName(const std::string& type,
                                                   const std::string& ctxPath, int ctxLine1) const
{
    if (type.empty() || m_typeDepth > 16) return std::nullopt;
    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& depth) : d(depth) { ++d; }
        ~DepthGuard() { --d; }
    } guard{m_typeDepth};
    if (type[0] == '$' && type.rfind("$unit::", 0) != 0) return std::nullopt; // container tag

    std::vector<std::string> segs;
    for (size_t start = 0;;) {
        size_t sep = type.find("::", start);
        segs.push_back(type.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
        if (sep == std::string::npos) break;
        start = sep + 2;
    }

    if (segs.size() > 1) {
        const std::string name = segs.back();
        segs.pop_back();
        auto scope = resolveQualifierScope(segs, ctxPath, ctxLine1);
        if (!scope) return std::nullopt;
        auto m = memberInScope(*scope, name, KindFilter::ScopeLike);
        if (m && m->kind == "Class") return m;
        if (auto td = memberInScope(*scope, name, KindFilter::Typedef))
            return resolveTypeName(td->detail, td->filePath, td->line);
        return std::nullopt;
    }

    // Inherited members are skipped: a type name (notably a class's own
    // `extends` name, looked up from the class's declaration line) resolves
    // in the lexical scopes around it, and consulting the class's own
    // ancestors here would recurse back into resolving that same name.
    if (auto r = lookupBare(ctxPath, ctxLine1, type, KindFilter::ScopeLike, /*withInherited=*/false);
        r && r->kind == "Class")
        return r;
    if (auto td = lookupBare(ctxPath, ctxLine1, type, KindFilter::Typedef, /*withInherited=*/false))
        return resolveTypeName(td->detail, td->filePath, td->line);
    std::vector<SymbolRow> classes, typedefs;
    for (auto& r : m_db.findSymbolsByName(type)) {
        if (r.kind == "Class") classes.push_back(r);
        if (r.kind == "Typedef") typedefs.push_back(r);
    }
    if (classes.size() == 1) return classes.front();
    if (classes.empty() && typedefs.size() == 1)
        return resolveTypeName(typedefs.front().detail, typedefs.front().filePath,
                               typedefs.front().line);
    return std::nullopt;
}

// The scope a `A::B::` qualifier names: a package, a class (resolved as a
// type from the context), `$unit` (top level), then nested classes.
std::optional<std::string> Resolver::resolveQualifierScope(const std::vector<std::string>& segs,
                                                           const std::string& ctxPath,
                                                           int ctxLine1) const
{
    if (segs.empty()) return std::nullopt;
    std::string scope;
    if (segs[0] == "$unit") {
        scope = "";
    } else {
        bool isPackage = false;
        for (auto& r : m_db.findSymbolsByName(segs[0]))
            if (r.kind == "Package") { isPackage = true; break; }
        if (isPackage) {
            scope = segs[0];
        } else if (auto cls = resolveTypeName(segs[0], ctxPath, ctxLine1)) {
            scope = qualifiedScopeOf(*cls);
        } else {
            return std::nullopt;
        }
    }
    for (size_t i = 1; i < segs.size(); ++i) {
        auto nested = memberInScope(scope, segs[i], KindFilter::ScopeLike);
        if (!nested || nested->kind != "Class") return std::nullopt;
        scope = qualifiedScopeOf(*nested);
    }
    return scope;
}

// `name` declared directly in `scope`, or -- when `scope` is a class -- in
// the nearest `extends` ancestor that declares it. Rows matching `prefer`
// win over other same-named rows at the same level.
std::optional<SymbolRow> Resolver::memberInScope(const std::string& scope, const std::string& name,
                                                 KindFilter prefer) const
{
    auto pick = [&](const std::vector<SymbolRow>& rows) -> std::optional<SymbolRow> {
        std::vector<const SymbolRow*> any, preferred;
        for (auto& r : rows) {
            if (r.name != name) continue;
            any.push_back(&r);
            if (kindMatches(r, prefer)) preferred.push_back(&r);
        }
        if (!preferred.empty()) return *earliest(preferred);
        if (prefer != KindFilter::ScopeLike && !any.empty()) return *earliest(any);
        return std::nullopt;
    };

    if (auto cls = classRowForScope(scope)) {
        for (auto& c : classChainRows(*cls))
            if (auto m = pick(m_db.findSymbolsInScope(qualifiedScopeOf(c)))) return m;
        return std::nullopt;
    }
    return pick(m_db.findSymbolsInScope(scope));
}

// The class a value/function row's declared type names, resolved where the
// row itself was declared.
std::optional<SymbolRow> Resolver::typeOfRow(const SymbolRow& row, int indexDepth) const
{
    std::string type;
    if (row.kind == "Signal" || row.kind == "Parameter")
        type = peelDimensionLayers(row.detail, indexDepth);
    else if (isCallable(row) && indexDepth == 0)
        type = row.detail; // return type
    else
        return std::nullopt; // Ports: not tracked as receivers yet (plan.md §6.30)
    if (type.empty()) return std::nullopt;
    if (firstTypeLayer(type) != type) return std::nullopt; // still a container, not a class
    return resolveTypeName(type, row.filePath, row.line);
}

std::optional<SymbolRow> Resolver::enclosingClassRow(int line1) const
{
    const std::string name = m_db.enclosingClassNameAt(m_path, line1);
    if (name.empty()) return std::nullopt;
    std::optional<SymbolRow> best;
    for (auto& r : m_db.findSymbolsByName(name))
        if (r.kind == "Class" && r.filePath == m_path && r.line <= line1 && line1 <= r.endLine &&
            (!best || r.line > best->line))
            best = r;
    if (best) return best;
    // An out-of-class method body (plan.md §6.30 step D): outside the
    // class's lines, but the class is on the body's scope chain.
    for (const auto& scope : scopeChain(m_path, line1))
        if (auto cls = classRowForScope(scope)) return cls;
    return std::nullopt;
}

std::optional<SymbolRow> Resolver::resolveReceiver(const std::vector<ChainSegment>& segs,
                                                   int line1) const
{
    if (segs.empty()) return std::nullopt;
    const auto& first = segs[0];
    std::optional<SymbolRow> cls;
    if (!first.isCall && first.indexDepth == 0 && first.name == "this") {
        cls = enclosingClassRow(line1);
    } else if (!first.isCall && first.indexDepth == 0 && first.name == "super") {
        auto self = enclosingClassRow(line1);
        if (!self) return std::nullopt;
        auto chain = classChainRows(*self);
        if (chain.size() < 2) return std::nullopt;
        cls = chain[1];
    } else {
        auto row = lookupBare(m_path, line1, first.name,
                              first.isCall ? KindFilter::Callable : KindFilter::Value);
        if (!row) return std::nullopt;
        cls = typeOfRow(*row, first.indexDepth);
    }

    for (size_t i = 1; cls && i < segs.size(); ++i) {
        auto m = memberInScope(qualifiedScopeOf(*cls), segs[i].name,
                               segs[i].isCall ? KindFilter::Callable : KindFilter::Value);
        if (!m) return std::nullopt;
        cls = typeOfRow(*m, segs[i].indexDepth);
    }
    return cls;
}

// The constructor `classScope` itself declares -- the earliest `new` row, so
// an extern prototype wins over its out-of-class body. Never an ancestor's:
// constructors are not inherited.
std::optional<SymbolRow> Resolver::constructorIn(const std::string& classScope) const
{
    std::vector<SymbolRow> rows;
    for (auto& r : m_db.findSymbolsInScope(classScope))
        if (r.name == "new" && isCallable(r)) rows.push_back(r);
    std::vector<const SymbolRow*> ptrs;
    for (auto& r : rows) ptrs.push_back(&r);
    if (ptrs.empty()) return std::nullopt;
    return *earliest(ptrs);
}

// `new` at [start, start+3), `before` its start with whitespace skipped. It
// names a constructor only by context -- never by name alone, which would
// pick an arbitrary class's:
//   - a constructor's own declaration (`function new`, `function C::new`);
//   - `super.new` (the parent class's), `C::new` / `p::C::new`;
//   - `lhs = new` / `T v = new` (the class of `lhs`, resolved as a value
//     chain; a declaration's own variable row gives its type).
// Anything else (`new[n]`, a copy `new obj`, `return new`, an argument) is
// nullopt.
std::optional<ResolvedSymbol> Resolver::resolveNew(size_t start, size_t before, int line1) const
{
    const auto pos = positionForOffset(m_text, start);
    for (auto& r : m_db.findSymbolsByName("new"))
        if (isCallable(r) && r.filePath == m_path && r.line == line1 &&
            r.col == static_cast<int>(pos.character))
            if (auto own = constructorIn(r.scope)) return ResolvedSymbol{*own, true};

    auto ctorOf = [&](const std::optional<SymbolRow>& cls) -> std::optional<ResolvedSymbol> {
        if (!cls) return std::nullopt;
        if (auto own = constructorIn(qualifiedScopeOf(*cls))) return ResolvedSymbol{*own, true};
        return std::nullopt;
    };

    if (scopeOpBefore(m_text, before)) {
        std::vector<std::string> segs;
        size_t i = before - 2;
        while (true) {
            skipWsBack(m_text, i);
            size_t segStart = identStartBefore(m_text, i);
            if (segStart == i) return std::nullopt;
            segs.push_back(m_text.substr(segStart, i - segStart));
            i = segStart;
            size_t k = i;
            skipWsBack(m_text, k);
            if (!scopeOpBefore(m_text, k)) break;
            i = k - 2;
        }
        std::reverse(segs.begin(), segs.end());
        auto scope = resolveQualifierScope(segs, m_path, line1);
        return scope ? ctorOf(classRowForScope(*scope)) : std::nullopt;
    }

    if (before > 0 && m_text[before - 1] == '.') {
        const lsp::Position endPos = positionForOffset(m_text, start + 3);
        auto dotCtx = dotCompletionContext(m_text, endPos.line, endPos.character);
        if (!dotCtx || dotCtx->segments.size() != 1 || dotCtx->segments[0].name != "super")
            return std::nullopt;
        return ctorOf(resolveReceiver(dotCtx->segments, line1));
    }

    // `lhs = new`: re-read `lhs` as a receiver chain by appending a '.'.
    if (before < 2 || m_text[before - 1] != '=' ||
        std::string_view("=!<>+-*/%&|^").find(m_text[before - 2]) != std::string_view::npos)
        return std::nullopt;
    size_t lhsEnd = before - 1;
    skipWsBack(m_text, lhsEnd);
    std::string probe = m_text.substr(0, lhsEnd) + ".";
    const lsp::Position probePos = positionForOffset(probe, probe.size());
    auto lhs = dotCompletionContext(probe, probePos.line, probePos.character);
    if (!lhs || lhs->segments.empty()) return std::nullopt;
    return ctorOf(resolveReceiver(lhs->segments, line1));
}

// For a named connection inside the parens at `paren`: the module/interface/
// program being instantiated (port list, or `#(` parameter list), or the
// function/task being called.
std::optional<Resolver::Owner> Resolver::connectionOwner(size_t paren, int depth)
{
    auto ownerAt = [&](size_t identStart) -> std::optional<SymbolRow> {
        auto r = resolveAt(identStart, depth + 1);
        if (!r) return std::nullopt;
        return r->row;
    };

    size_t i = paren;
    skipWsBack(m_text, i);

    // `Type #(` -- a parameter value assignment list.
    if (i > 0 && m_text[i - 1] == '#') {
        size_t j = i - 1;
        skipWsBack(m_text, j);
        size_t start = identStartBefore(m_text, j);
        if (start == j) return std::nullopt;
        auto owner = ownerAt(start);
        if (owner && (isDesignUnit(*owner) || owner->kind == "Class")) return Owner{*owner, true};
        return std::nullopt;
    }

    const size_t xStart = identStartBefore(m_text, i);
    if (xStart == i) return std::nullopt;
    size_t j = xStart;
    skipWsBack(m_text, j);

    // `Type #(...) inst (`
    if (j > 0 && m_text[j - 1] == ')') {
        size_t open = matchingOpenParen(m_text, j - 1);
        if (open == std::string::npos) return std::nullopt;
        size_t k = open;
        skipWsBack(m_text, k);
        if (k == 0 || m_text[k - 1] != '#') return std::nullopt;
        --k;
        skipWsBack(m_text, k);
        size_t tStart = identStartBefore(m_text, k);
        if (tStart == k) return std::nullopt;
        auto owner = ownerAt(tStart);
        if (owner && isDesignUnit(*owner)) return Owner{*owner, false};
        return std::nullopt;
    }

    // `Type inst (`
    if (j > 0 && isIdChar(m_text[j - 1])) {
        size_t tStart = identStartBefore(m_text, j);
        if (auto owner = ownerAt(tStart); owner && isDesignUnit(*owner)) return Owner{*owner, false};
    }

    // `callee (` -- a function/task called with named arguments.
    if (auto owner = ownerAt(xStart); owner && isCallable(*owner)) return Owner{*owner, false};
    return std::nullopt;
}

std::optional<ResolvedSymbol> Resolver::resolveAt(size_t offset, int depth)
{
    if (depth > kMaxDepth || offset > m_text.size()) return std::nullopt;

    size_t start = offset, end = offset;
    while (start > 0 && isIdChar(m_text[start - 1])) --start;
    while (end < m_text.size() && isIdChar(m_text[end])) ++end;
    if (start == end) return std::nullopt;
    const std::string word = m_text.substr(start, end - start);
    if (std::isdigit(static_cast<unsigned char>(word[0]))) return std::nullopt;
    const int line1 = line1At(m_text, start);

    const size_t after = skipWsFwd(m_text, end);
    const bool followedByScope = after + 1 < m_text.size() && m_text[after] == ':' &&
                                 m_text[after + 1] == ':';
    const bool followedByParen = after < m_text.size() && m_text[after] == '(';

    size_t before = start;
    skipWsBack(m_text, before);

    if (word == "new") return resolveNew(start, before, line1);

    // 1. `A::B::word`
    if (scopeOpBefore(m_text, before)) {
        std::vector<std::string> segs;
        size_t i = before - 2;
        while (true) {
            skipWsBack(m_text, i);
            if (i > 0 && m_text[i - 1] == ')') { // `C#(params)::`
                size_t open = matchingOpenParen(m_text, i - 1);
                if (open == std::string::npos) return fallback(word);
                i = open;
                skipWsBack(m_text, i);
                if (i == 0 || m_text[i - 1] != '#') return fallback(word);
                --i;
                skipWsBack(m_text, i);
            }
            size_t segStart = identStartBefore(m_text, i);
            if (segStart == i) return fallback(word);
            segs.push_back(m_text.substr(segStart, i - segStart));
            i = segStart;
            size_t k = i;
            skipWsBack(m_text, k);
            if (!scopeOpBefore(m_text, k)) break;
            i = k - 2;
        }
        std::reverse(segs.begin(), segs.end());
        auto scope = resolveQualifierScope(segs, m_path, line1);
        if (!scope) return fallback(word);
        const KindFilter prefer = followedByScope ? KindFilter::ScopeLike
                                : followedByParen ? KindFilter::Callable
                                                  : KindFilter::Any;
        if (auto m = memberInScope(*scope, word, prefer)) return ResolvedSymbol{*m, true};
        return std::nullopt;
    }

    // 2./3. `.word`
    if (before > 0 && m_text[before - 1] == '.') {
        size_t dot = before - 1;
        size_t k = dot;
        skipWsBack(m_text, k);
        if (k == 0 || m_text[k - 1] == '(' || m_text[k - 1] == ',') {
            auto paren = findEnclosingParen(m_text, dot);
            if (!paren) return fallback(word);
            auto owner = connectionOwner(*paren, depth);
            if (!owner) return fallback(word);
            for (auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(owner->row)))
                if (r.name == word && r.kind == (owner->paramList ? "Parameter" : "Port"))
                    return ResolvedSymbol{r, true};
            return std::nullopt;
        }

        const lsp::Position endPos = positionForOffset(m_text, end);
        auto dotCtx = dotCompletionContext(m_text, endPos.line, endPos.character);
        if (!dotCtx || dotCtx->prefix != word) return fallback(word);
        auto cls = resolveReceiver(dotCtx->segments, line1);
        if (!cls) return fallback(word);
        const KindFilter prefer = followedByParen ? KindFilter::Callable : KindFilter::Value;
        if (auto m = memberInScope(qualifiedScopeOf(*cls), word, prefer))
            return ResolvedSymbol{*m, true};
        return std::nullopt;
    }

    // 4. bare `word`
    const KindFilter filter = followedByScope ? KindFilter::ScopeLike : KindFilter::Any;
    if (auto r = lookupBare(m_path, line1, word, filter)) return ResolvedSymbol{*r, true};
    return fallback(word);
}

} // namespace

std::optional<ResolvedSymbol> resolveSymbolAt(SymbolDatabase& db, const std::string& path,
                                              const std::string& text, unsigned line,
                                              unsigned character)
{
    auto offset = offsetOf(text, line, character);
    if (!offset) return std::nullopt;
    return Resolver(db, path, text).resolveAt(*offset, 0);
}

std::vector<std::optional<ResolvedSymbol>> resolveSymbolsAt(
    SymbolDatabase& db, const std::string& path, const std::string& text,
    const std::vector<std::pair<unsigned, unsigned>>& positions)
{
    Resolver resolver(db, path, text);
    std::vector<std::optional<ResolvedSymbol>> results;
    results.reserve(positions.size());
    for (auto [line, character] : positions) {
        auto offset = offsetOf(text, line, character);
        results.push_back(offset ? resolver.resolveAt(*offset, 0) : std::nullopt);
    }
    return results;
}

int64_t overrideFamilyId(SymbolDatabase& db, const SymbolRow& row)
{
    const std::string noText;
    return Resolver(db, row.filePath, noText).overrideFamilyId(row);
}
