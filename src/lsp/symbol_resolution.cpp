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

SrcPos posAt(const std::string& text, size_t offset)
{
    const size_t lineStart = offset == 0 ? 0 : text.rfind('\n', offset - 1) + 1; // npos + 1 == 0
    return {static_cast<int>(std::count(text.begin(), text.begin() + offset, '\n')) + 1,
            static_cast<int>(offset - lineStart)};
}

SrcPos posOf(const SymbolRow& r) { return {r.line, r.col}; }

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

// Value: anything a `.` can follow -- a variable, a struct member, an
// instance, a named block or a design unit named from the top
// (`top.u_sub.sig`).
enum class KindFilter { Any, ScopeLike, Callable, Value, Typedef };

bool kindMatches(const SymbolRow& r, KindFilter f)
{
    switch (f) {
    case KindFilter::Any:       return true;
    case KindFilter::ScopeLike: return r.kind == "Package" || r.kind == "Class";
    case KindFilter::Callable:  return isCallable(r);
    case KindFilter::Value:
        return r.kind == "Signal" || r.kind == "Parameter" || r.kind == "Port" ||
               r.kind == "Member" || r.kind == "Instance" || r.kind == "Block" || isDesignUnit(r);
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

// The last of `rows` declared at or before `at`, else the earliest: the
// nearest preceding declaration in a non-class scope -- an out-of-class
// body's own argument rather than the extern prototype's same-named one,
// both in scope `C::m` (plan.md §6.30 step D).
const SymbolRow* closestBefore(const std::vector<const SymbolRow*>& rows, SrcPos at)
{
    const SymbolRow* best = nullptr;
    for (auto* r : rows)
        if ((r->line < at.line || (r->line == at.line && r->col <= at.col)) &&
            (!best || r->line > best->line || (r->line == best->line && r->col > best->col)))
            best = r;
    return best ? best : earliest(rows);
}

class Resolver {
public:
    // resolveReceiverType's entry point.
    std::optional<ReceiverType> receiverTypeOf(const std::vector<ChainSegment>& segs, SrcPos at) const
    {
        return receiverType(segs, at);
    }

    Resolver(SymbolDatabase& db, const std::string& path, const std::string& text)
        : m_db(db), m_path(path), m_text(blankCommentsAndStrings(text)) {}

    std::optional<ResolvedSymbol> resolveAt(size_t offset, int depth);
    SymbolRow overrideFamilyRoot(const SymbolRow& row) const;

private:
    SymbolDatabase&    m_db;
    const std::string& m_path;
    const std::string  m_text; // comments/strings blanked, same offsets
    mutable int        m_typeDepth = 0; // guards resolveTypeName's own recursion

    std::optional<ResolvedSymbol> fallback(const std::string& name) const;

    std::vector<std::string> scopeChain(const std::string& path, SrcPos at) const;
    std::optional<SymbolRow> classRowForScope(const std::string& scope) const;
    std::vector<SymbolRow> classChainRows(const SymbolRow& cls) const;
    std::optional<SymbolRow> lookupBare(const std::string& path, SrcPos at,
                                        const std::string& name, KindFilter filter,
                                        bool withInherited = true) const;
    std::optional<SymbolRow> resolveTypeName(const std::string& type, const std::string& ctxPath,
                                             SrcPos ctxAt, bool valueType = false) const;
    std::optional<SymbolRow> followTypedef(const SymbolRow& td, bool valueType) const;
    bool hasMembers(const SymbolRow& row) const;
    std::optional<SymbolRow> classInScope(const std::string& scope, const std::string& name,
                                          bool valueType = false) const;
    std::optional<std::string> resolveQualifierScope(const std::vector<std::string>& segs,
                                                     const std::string& ctxPath,
                                                     SrcPos ctxAt) const;
    std::optional<SymbolRow> memberInScope(const std::string& scope, const std::string& name,
                                           KindFilter prefer) const;
    ReceiverType typeOfRow(const SymbolRow& row, int indexDepth) const;
    std::optional<SymbolRow> enclosingClassRow(SrcPos at) const;
    std::optional<ReceiverType> receiverType(const std::vector<ChainSegment>& segs,
                                             SrcPos at) const;
    std::optional<SymbolRow> resolveReceiver(const std::vector<ChainSegment>& segs,
                                             SrcPos at) const;
    std::optional<SymbolRow> constructorIn(const std::string& classScope) const;
    std::optional<ResolvedSymbol> resolveNew(size_t start, size_t before, SrcPos at, int depth);
    std::optional<SymbolRow> enclosingFunctionRow(SrcPos at) const;
    std::optional<SymbolRow> argumentType(size_t before, size_t start, int depth);
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
std::vector<std::string> Resolver::scopeChain(const std::string& path, SrcPos at) const
{
    return SymbolDatabase::scopeChain(m_db.scopeAtPosition(path, at));
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
        auto parent = resolveTypeName(chain.back().detail, chain.back().filePath, posOf(chain.back()));
        if (!parent || !seen.insert(qualifiedScopeOf(*parent)).second) break;
        chain.push_back(*parent);
    }
    return chain;
}

// The id of the topmost ancestor's same-named Function/Task for a class
// method (the root of its override family), else `row.id`.
SymbolRow Resolver::overrideFamilyRoot(const SymbolRow& row) const
{
    if (!isCallable(row)) return row;
    // Constructors are never inherited or overridden: each class's own `new`
    // (its extern prototype and out-of-class body alike) is one symbol.
    if (row.name == "new") {
        auto own = constructorIn(row.scope);
        return own ? *own : row;
    }
    auto cls = classRowForScope(row.scope);
    if (!cls) return row;
    SymbolRow root = row;
    const auto chain = classChainRows(*cls);
    for (size_t i = 1; i < chain.size(); ++i)
        for (const auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(chain[i]), row.name))
            if (isCallable(r)) {
                root = r;
                break;
            }
    return root;
}

std::optional<SymbolRow> Resolver::lookupBare(const std::string& path, SrcPos at,
                                              const std::string& name, KindFilter filter,
                                              bool withInherited) const
{
    const auto chain   = scopeChain(path, at);
    const auto visible = m_db.findSymbolsVisibleAt(path, at, name, &chain);

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
        if (!hits.empty()) return cls ? *earliest(hits) : *closestBefore(hits, at);
        if (!cls) {
            // A package's members from any file: a package is often spread
            // over `include'd files (the visible set has them all).
            if (scope.empty() || !m_db.isPackage(scope)) continue;
            std::vector<SymbolRow> own;
            for (const auto& r : visible)
                if (r.scope == scope && r.name == name && kindMatches(r, filter)) own.push_back(r);
            if (!own.empty()) return *pickBestSymbol(own, path);
            continue;
        }
        // The class's own members from any file: an out-of-class body
        // (plan.md §6.30 step D) may live in a different file than its class,
        // and the visible set's local chain only covers the cursor's file.
        {
            std::vector<SymbolRow> own;
            for (auto& r : m_db.findSymbolsInScope(scope, name))
                if (kindMatches(r, filter)) own.push_back(r);
            std::vector<const SymbolRow*> ptrs;
            for (auto& r : own) ptrs.push_back(&r);
            if (!ptrs.empty()) return *earliest(ptrs);
        }
        if (!withInherited) continue;
        auto ancestors = classChainRows(*cls);
        for (size_t i = 1; i < ancestors.size(); ++i)
            for (auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(ancestors[i]), name))
                if (kindMatches(r, filter)) return r;
    }

    auto isLocal = [&](const SymbolRow& r) {
        return r.filePath == path && std::find(chain.begin(), chain.end(), r.scope) != chain.end();
    };

    // 2. A specific import (`import pkg::name;`) written in a scope on the
    //    chain (an import in another module of the file doesn't apply), in
    //    the file or in an includer of it.
    const auto imports = m_db.importsVisibleFrom(path);
    for (const auto& r : visible) {
        if (isLocal(r) || r.name != name || !kindMatches(r, filter)) continue;
        for (const auto& imp : imports)
            if (imp.item == name && imp.pkgName == r.scope &&
                std::find(chain.begin(), chain.end(), imp.scope) != chain.end())
                return r;
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
// (ctxPath, ctxAt) -- the declaration that wrote it. A Typedef is
// followed to the type it aliases, resolved where the typedef is declared.
// A bare name nothing visible declares still resolves if exactly one class
// (or else exactly one typedef) has that name.
//
// With `valueType` -- the type of a value a `.` can follow -- a struct/union
// typedef (one with Member rows) is a result too, and a name no class or
// typedef has resolves to the Interface/Module/Program of that name (a
// virtual interface's `intf.mp` drops its modport).
std::optional<SymbolRow> Resolver::resolveTypeName(const std::string& typeIn,
                                                   const std::string& ctxPath, SrcPos ctxAt,
                                                   bool valueType) const
{
    if (typeIn.empty() || m_typeDepth > 16) return std::nullopt;
    const std::string type =
        valueType && typeIn.find('.') != std::string::npos ? typeIn.substr(0, typeIn.find('.'))
                                                           : typeIn;
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
        auto scope = resolveQualifierScope(segs, ctxPath, ctxAt);
        if (!scope) return std::nullopt;
        return classInScope(*scope, name, valueType);
    }

    // Inherited members are skipped: a type name (notably a class's own
    // `extends` name, looked up from the class's declaration line) resolves
    // in the lexical scopes around it, and consulting the class's own
    // ancestors here would recurse back into resolving that same name.
    if (auto r = lookupBare(ctxPath, ctxAt, type, KindFilter::ScopeLike, /*withInherited=*/false);
        r && r->kind == "Class")
        return r;
    // A forward typedef visible here means the class itself isn't (the
    // visible set drops the forward row wherever it is): its detail is a
    // keyword, never followed -- go on to the name-only search.
    if (auto td = lookupBare(ctxPath, ctxAt, type, KindFilter::Typedef, /*withInherited=*/false);
        td && !isForwardTypedef(*td))
        return followTypedef(*td, valueType);
    std::vector<SymbolRow> classes, typedefs, units;
    for (auto& r : m_db.findSymbolsByName(type)) {
        if (r.kind == "Class") classes.push_back(r);
        if (r.kind == "Typedef" && !isForwardTypedef(r)) typedefs.push_back(r);
        if (isDesignUnit(r) && r.scope.empty()) units.push_back(r);
    }
    if (classes.size() == 1) return classes.front();
    if (classes.empty() && typedefs.size() == 1) return followTypedef(typedefs.front(), valueType);
    if (valueType && classes.empty() && typedefs.empty() && !units.empty())
        return *pickBestSymbol(units, ctxPath);
    return std::nullopt;
}

// The type a (non-forward) typedef aliases, resolved where it is declared;
// with `valueType`, a struct/union typedef (no aliased name, Member rows of
// its own) is that type itself.
std::optional<SymbolRow> Resolver::followTypedef(const SymbolRow& td, bool valueType) const
{
    if (isForwardTypedef(td)) return std::nullopt;
    if (td.detail.empty()) {
        if (valueType && hasMembers(td)) return td;
        return std::nullopt;
    }
    return resolveTypeName(td.detail, td.filePath, posOf(td), valueType);
}

// True if struct/union Member rows are scoped under `row` -- a struct
// typedef, or a variable/member declared with an inline struct type.
bool Resolver::hasMembers(const SymbolRow& row) const
{
    for (const auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(row)))
        if (r.kind == "Member") return true;
    return false;
}

// The class `name` names as a member of `scope`: a class declared there, or
// a typedef declared there followed to the class it aliases (or, with
// `valueType`, to its struct; see resolveTypeName).
std::optional<SymbolRow> Resolver::classInScope(const std::string& scope,
                                                const std::string& name, bool valueType) const
{
    auto m = memberInScope(scope, name, KindFilter::ScopeLike);
    if (m && m->kind == "Class") return m;
    if (auto td = memberInScope(scope, name, KindFilter::Typedef); td && td->kind == "Typedef")
        return followTypedef(*td, valueType);
    return std::nullopt;
}

// The scope a `A::B::` qualifier names: a package, a class (resolved as a
// type from the context), `$unit` (top level), then nested classes -- a
// nested typedef (UVM's `C::type_id::`) followed to the class it aliases.
std::optional<std::string> Resolver::resolveQualifierScope(const std::vector<std::string>& segs,
                                                           const std::string& ctxPath,
                                                           SrcPos ctxAt) const
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
        } else if (auto cls = resolveTypeName(segs[0], ctxPath, ctxAt)) {
            scope = qualifiedScopeOf(*cls);
        } else {
            return std::nullopt;
        }
    }
    for (size_t i = 1; i < segs.size(); ++i) {
        auto nested = classInScope(scope, segs[i]);
        if (!nested) return std::nullopt;
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
            if (auto m = pick(m_db.findSymbolsInScope(qualifiedScopeOf(c), name))) return m;
        return std::nullopt;
    }
    return pick(m_db.findSymbolsInScope(scope, name));
}

// The type of `row`'s value after `indexDepth` index groups, resolved where
// the row itself was declared: the row owning its members (see
// ReceiverType), else the remaining type text for a container or a type
// that doesn't resolve. A named block is its own owner (indexed for a
// generate loop's `gen[0]`), and so is a design unit named from the top, a
// struct-typed variable declared with an inline struct.
ReceiverType Resolver::typeOfRow(const SymbolRow& row, int indexDepth) const
{
    if (row.kind == "Block" || (isDesignUnit(row) && indexDepth == 0)) return {row, ""};
    std::string detail;
    if (row.kind == "Signal" || row.kind == "Parameter" || row.kind == "Member" ||
        row.kind == "Instance")
        detail = row.detail;
    else if (row.kind == "Port")
        detail = portValueType(row.detail);
    else if (isCallable(row) && indexDepth == 0)
        detail = row.detail; // return type
    else
        return {};
    const std::string type = peelDimensionLayers(detail, indexDepth);
    if (type.empty()) {
        // `struct {...} s[4]` has detail "$fixed_array" and members under `s`.
        const auto layers = splitLayers(detail);
        const bool allPeeled = static_cast<int>(layers.size()) == indexDepth &&
                               std::all_of(layers.begin(), layers.end(), isContainerDimensionTag);
        if ((detail.empty() || allPeeled) && row.kind != "Instance" && hasMembers(row))
            return {row, ""};
        return {};
    }
    if (firstTypeLayer(type) != type || (type[0] == '$' && type.rfind("$unit::", 0) != 0))
        return {std::nullopt, type}; // a container or a built-in type
    if (auto owner = resolveTypeName(type, row.filePath, posOf(row), /*valueType=*/true))
        return {owner, ""};
    return {std::nullopt, type};
}

std::optional<SymbolRow> Resolver::enclosingClassRow(SrcPos at) const
{
    const std::string name = m_db.enclosingClassNameAt(m_path, at);
    if (name.empty()) return std::nullopt;
    std::optional<SymbolRow> best;
    for (auto& r : m_db.findSymbolsByName(name))
        if (r.kind == "Class" && r.filePath == m_path && r.line <= at.line && at.line <= r.endLine &&
            (!best || r.line > best->line))
            best = r;
    if (best) return best;
    // An out-of-class method body (plan.md §6.30 step D): outside the
    // class's lines, but the class is on the body's scope chain.
    for (const auto& scope : scopeChain(m_path, at))
        if (auto cls = classRowForScope(scope)) return cls;
    return std::nullopt;
}

// The type a receiver chain's value has; nullopt when a segment resolves
// to nothing, or a segment before the last has no member-owning type.
std::optional<ReceiverType> Resolver::receiverType(const std::vector<ChainSegment>& segs,
                                                   SrcPos at) const
{
    if (segs.empty()) return std::nullopt;
    const auto& first = segs[0];
    ReceiverType type;
    if (!first.isCall && first.indexDepth == 0 && first.name == "this") {
        type.owner = enclosingClassRow(at);
    } else if (!first.isCall && first.indexDepth == 0 && first.name == "super") {
        auto self = enclosingClassRow(at);
        if (!self) return std::nullopt;
        auto chain = classChainRows(*self);
        if (chain.size() < 2) return std::nullopt;
        type.owner = chain[1];
    } else {
        auto row = lookupBare(m_path, at, first.name,
                              first.isCall ? KindFilter::Callable : KindFilter::Value);
        if (!row) return std::nullopt;
        type = typeOfRow(*row, first.indexDepth);
    }

    for (size_t i = 1; i < segs.size(); ++i) {
        if (!type.owner) return std::nullopt;
        auto m = memberInScope(qualifiedScopeOf(*type.owner), segs[i].name,
                               segs[i].isCall ? KindFilter::Callable : KindFilter::Value);
        if (!m) return std::nullopt;
        type = typeOfRow(*m, segs[i].indexDepth);
    }
    if (!type.owner && type.builtin.empty()) return std::nullopt;
    return type;
}

// The row owning the receiver chain's members (see ReceiverType).
std::optional<SymbolRow> Resolver::resolveReceiver(const std::vector<ChainSegment>& segs,
                                                   SrcPos at) const
{
    auto type = receiverType(segs, at);
    return type ? type->owner : std::nullopt;
}

// The constructor `classScope` itself declares -- the earliest `new` row, so
// an extern prototype wins over its out-of-class body. Never an ancestor's:
// constructors are not inherited.
std::optional<SymbolRow> Resolver::constructorIn(const std::string& classScope) const
{
    std::vector<SymbolRow> rows;
    for (auto& r : m_db.findSymbolsInScope(classScope, "new"))
        if (isCallable(r)) rows.push_back(r);
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
//     chain; a declaration's own variable row gives its type);
//   - `return new` (the enclosing function's return type);
//   - an argument, `f(x, new)` / `f(.p(new))` (the callee's parameter type).
// Anything else (`new[n]`, a copy `new obj`) is nullopt.
std::optional<ResolvedSymbol> Resolver::resolveNew(size_t start, size_t before, SrcPos at,
                                                   int depth)
{
    const auto pos = positionForOffset(m_text, start);
    for (auto& r : m_db.findSymbolsByName("new"))
        if (isCallable(r) && r.filePath == m_path && r.line == at.line &&
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
        auto scope = resolveQualifierScope(segs, m_path, at);
        return scope ? ctorOf(classRowForScope(*scope)) : std::nullopt;
    }

    if (before > 0 && m_text[before - 1] == '.') {
        const lsp::Position endPos = positionForOffset(m_text, start + 3);
        auto dotCtx = dotCompletionContext(m_text, endPos.line, endPos.character);
        if (!dotCtx || dotCtx->segments.size() != 1 || dotCtx->segments[0].name != "super")
            return std::nullopt;
        return ctorOf(resolveReceiver(dotCtx->segments, at));
    }

    if (const size_t kw = identStartBefore(m_text, before);
        m_text.compare(kw, before - kw, "return") == 0) {
        auto fn = enclosingFunctionRow(at);
        return fn ? ctorOf(typeOfRow(*fn, 0).owner) : std::nullopt;
    }

    if (before > 0 && (m_text[before - 1] == '(' || m_text[before - 1] == ','))
        return ctorOf(argumentType(before, start, depth));

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
    return ctorOf(resolveReceiver(lhs->segments, at));
}

// The Function whose body holds `at`: the innermost function frame on
// the scope chain, its first declaration (an extern prototype before its
// out-of-class body). nullopt inside a task, or outside any function.
std::optional<SymbolRow> Resolver::enclosingFunctionRow(SrcPos at) const
{
    for (const auto& scope : scopeChain(m_path, at)) {
        const auto sep = scope.rfind("::");
        if (sep == std::string::npos) continue; // a top-level unit, not a body
        const std::string parent = scope.substr(0, sep), name = scope.substr(sep + 2);
        std::vector<SymbolRow> rows;
        for (auto& r : m_db.findSymbolsInScope(parent, name))
            if (isCallable(r)) rows.push_back(r);
        if (rows.empty()) continue;
        std::vector<const SymbolRow*> ptrs;
        for (auto& r : rows) ptrs.push_back(&r);
        const SymbolRow* fn = earliest(ptrs);
        if (fn->kind != "Function") return std::nullopt;
        return *fn;
    }
    return std::nullopt;
}

// The class of the argument `new` at `start` is passed as -- `before` is
// just past the '(' or ',' in front of it: the callee's parameter named by
// `.name(`, else the one at its comma position. Only a call whose callee
// resolves exactly to a Function/Task (a constructor included).
std::optional<SymbolRow> Resolver::argumentType(size_t before, size_t start, int depth)
{
    std::optional<std::string> named;
    size_t argStart = start;
    if (m_text[before - 1] == '(' && isNamedConnectionParen(m_text, before - 1)) {
        size_t i = before - 1;
        skipWsBack(m_text, i);
        const size_t idStart = identStartBefore(m_text, i);
        named = m_text.substr(idStart, i - idStart);
        argStart = before - 1;
    }
    auto paren = findEnclosingParen(m_text, argStart);
    if (!paren) return std::nullopt;
    auto owner = connectionOwner(*paren, depth);
    if (!owner || owner->paramList || !isCallable(owner->row)) return std::nullopt;
    const auto ports = m_db.portsOf(qualifiedScopeOf(owner->row));

    const SymbolRow* port = nullptr;
    if (named) {
        for (auto& p : ports)
            if (p.name == *named) port = &p;
    } else {
        size_t index = 0;
        int nest = 0;
        for (size_t i = *paren + 1; i < start; ++i) {
            const char c = m_text[i];
            if (c == '(' || c == '[' || c == '{') ++nest;
            else if (c == ')' || c == ']' || c == '}') --nest;
            else if (c == ',' && nest == 0) ++index;
        }
        if (index < ports.size()) port = &ports[index];
    }
    return port ? typeOfRow(*port, 0).owner : std::nullopt;
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
    const SrcPos at = posAt(m_text, start);

    const size_t after = skipWsFwd(m_text, end);
    const bool followedByScope = after + 1 < m_text.size() && m_text[after] == ':' &&
                                 m_text[after + 1] == ':';
    const bool followedByParen = after < m_text.size() && m_text[after] == '(';

    size_t before = start;
    skipWsBack(m_text, before);

    if (word == "new") return resolveNew(start, before, at, depth);

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
        auto scope = resolveQualifierScope(segs, m_path, at);
        if (!scope) return fallback(word);
        const KindFilter prefer = followedByScope ? KindFilter::ScopeLike
                                : followedByParen ? KindFilter::Callable
                                                  : KindFilter::Any;
        if (auto m = memberInScope(*scope, word, prefer)) return ResolvedSymbol{*m, true};
        if (followedByScope)
            if (auto td = memberInScope(*scope, word, KindFilter::Typedef))
                return ResolvedSymbol{*td, true};
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
            for (auto& r : m_db.findSymbolsInScope(qualifiedScopeOf(owner->row), word))
                if (r.kind == (owner->paramList ? "Parameter" : "Port"))
                    return ResolvedSymbol{r, true};
            return std::nullopt;
        }

        const lsp::Position endPos = positionForOffset(m_text, end);
        auto dotCtx = dotCompletionContext(m_text, endPos.line, endPos.character);
        if (!dotCtx || dotCtx->prefix != word) return fallback(word);
        auto owner = resolveReceiver(dotCtx->segments, at);
        if (!owner) return fallback(word);
        const KindFilter prefer = followedByParen ? KindFilter::Callable : KindFilter::Value;
        if (auto m = memberInScope(qualifiedScopeOf(*owner), word, prefer))
            return ResolvedSymbol{*m, true};
        return std::nullopt;
    }

    // 4. bare `word`
    const KindFilter filter = followedByScope ? KindFilter::ScopeLike : KindFilter::Any;
    if (auto r = lookupBare(m_path, at, word, filter)) return ResolvedSymbol{*r, true};
    // A declaration no lexical chain reaches (a struct member) is still
    // itself when the cursor is on its name.
    for (auto& r : m_db.findSymbolsByName(word))
        if (r.filePath == m_path && r.line == at.line && r.col == at.col)
            return ResolvedSymbol{r, true};
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

std::vector<std::optional<SymbolRow>> resolveCallees(SymbolDatabase& db, const std::string& path,
                                                     const std::string& text,
                                                     const std::vector<CallRecord>& calls)
{
    auto isIdentChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
    };
    std::vector<std::optional<SymbolRow>> callees(calls.size());
    std::vector<std::pair<unsigned, unsigned>> positions;
    std::vector<size_t> callOf; // positions[i] is calls[callOf[i]]'s name
    for (size_t c = 0; c < calls.size(); ++c) {
        const CallRecord& call = calls[c];
        if (call.nameLine < 1 || call.nameColumn < 0) continue;
        auto offset = offsetOf(text, call.nameLine - 1, call.nameColumn);
        if (!offset) continue;
        const size_t end = *offset + call.calleeName.size();
        if (text.compare(*offset, call.calleeName.size(), call.calleeName) != 0 ||
            (end < text.size() && isIdentChar(text[end])))
            continue;
        positions.emplace_back(call.nameLine - 1, call.nameColumn);
        callOf.push_back(c);
    }

    auto resolved = resolveSymbolsAt(db, path, text, positions);
    for (size_t i = 0; i < resolved.size(); ++i) {
        const CallRecord& call = calls[callOf[i]];
        const auto& r = resolved[i];
        if (r && r->exact && isCallable(r->row)) {
            callees[callOf[i]] = r->row;
            continue;
        }
        if (!call.calleeScope.empty()) continue;
        std::vector<SymbolRow> byName;
        for (auto& row : db.findSymbolsByName(call.calleeName))
            if (isCallable(row)) byName.push_back(row);
        if (!byName.empty()) callees[callOf[i]] = SymbolDatabase::pickSameFilePreferred(byName, path);
    }
    return callees;
}

std::optional<ReceiverType> resolveReceiverType(SymbolDatabase& db, const std::string& path,
                                                const std::string& text,
                                                const std::vector<ChainSegment>& segments,
                                                SrcPos at)
{
    return Resolver(db, path, text).receiverTypeOf(segments, at);
}

SymbolRow overrideFamilyRoot(SymbolDatabase& db, const SymbolRow& row)
{
    const std::string noText;
    return Resolver(db, row.filePath, noText).overrideFamilyRoot(row);
}
