#include "lsp/completion.h"
#include "lsp/symbol_utils.h"
#include "lsp/fuzzy_match.h"
#include "lsp/sv_keywords.h"
#include "lsp/sv_builtin_methods.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

// A completion candidate, independent of its source (a DB symbol row or a
// synthetic SV keyword) -- lets keyword and symbol candidates share one
// fuzzy-score/sort/build pipeline.
struct Candidate {
    std::string              name;
    lsp::CompletionItemKind  kind;
    std::string              detail; // empty = omit item.detail
};

std::vector<Candidate> candidatesFromRows(const std::vector<SymbolRow>& rows)
{
    std::vector<Candidate> out;
    out.reserve(rows.size());
    for (auto& row : rows)
        out.push_back({row.name, completionKindFor(row.kind), row.detail});
    return out;
}

// Legal keywords at `scopeKind` (a SymbolDatabase::scopeKindAtPosition()
// result) as candidates, for merging into the non-dot completion pool.
std::vector<Candidate> candidatesFromKeywords(const std::string& scopeKind)
{
    const unsigned bit = contextBitFor(scopeKind);
    std::vector<Candidate> out;
    for (const auto& kw : SV_KEYWORDS) {
        if (kw.contexts & bit)
            out.push_back({std::string(kw.text), lsp::CompletionItemKind::Keyword, ""});
    }
    return out;
}

std::vector<Candidate> candidatesFromMethods(std::span<const BuiltinMethod> methods)
{
    std::vector<Candidate> out;
    out.reserve(methods.size());
    for (auto& m : methods)
        out.push_back({std::string(m.name), m.kind, std::string(m.detail)});
    return out;
}

// Fuzzy-filters (subsequence match, typo/skip tolerant) and ranks
// `candidates` against `prefix`, then builds the LSP item list. With no
// prefix every candidate stays in, unranked. Returns nullptr if
// `candidates` is empty, or if every candidate fails to match a non-empty
// prefix.
lsp::TextDocument_CompletionResult buildCompletionItems(
    const std::vector<Candidate>& candidates, const std::string& prefix)
{
    if (candidates.empty()) return nullptr;

    struct Scored { const Candidate* candidate; int score; };
    std::vector<Scored> scored;
    scored.reserve(candidates.size());
    for (auto& c : candidates) {
        int score = 0;
        if (!prefix.empty()) {
            auto s = fuzzyScore(c.name, prefix);
            if (!s) continue;
            score = *s;
        }
        scored.push_back({&c, score});
    }
    if (scored.empty()) return nullptr;

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.candidate->name < b.candidate->name;
    });

    lsp::Array<lsp::CompletionItem> items;
    items.reserve(scored.size());
    for (size_t i = 0; i < scored.size(); ++i) {
        const auto& c = *scored[i].candidate;

        lsp::CompletionItem item;
        item.label = c.name;
        item.kind  = c.kind;
        if (!c.detail.empty())
            item.detail = c.detail;
        if (!prefix.empty()) {
            // Zero-padded rank so clients that re-sort by sortText (rather
            // than trusting response order) preserve our fuzzy ranking.
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%05zu", i);
            item.sortText = std::string(buf);
        }
        items.push_back(std::move(item));
    }

    return items;
}

// A container/element detail string is a ':'-delimited list of layers,
// outermost first (plan.md §6.15 -- see containerDimensionTags() in
// src/compiler/sv_tree_walker.cpp for how it's built). Splits on ':'.
std::vector<std::string> splitLayers(const std::string& detail)
{
    std::vector<std::string> layers;
    size_t start = 0;
    while (start <= detail.size()) {
        size_t colon = detail.find(':', start);
        if (colon == std::string::npos) {
            layers.push_back(detail.substr(start));
            break;
        }
        layers.push_back(detail.substr(start, colon - start));
        start = colon + 1;
    }
    return layers;
}

// The outermost layer of a (possibly layered) detail string -- what
// container/element-tag dispatch (builtinMethodsFor) always keys off,
// regardless of how many further layers describe the element type. A
// single-layer detail (a bare class name, or any pre-§6.15 detail) is
// unaffected: its "first layer" is just itself.
std::string firstLayer(const std::string& detail)
{
    const size_t colon = detail.find(':');
    return colon == std::string::npos ? detail : detail.substr(0, colon);
}

bool isContainerDimensionTag(const std::string& layer)
{
    return layer == CONTAINER_QUEUE || layer == CONTAINER_ASSOC ||
           layer == CONTAINER_DYNAMIC_ARRAY || layer == CONTAINER_FIXED_ARRAY;
}

// Peels `depth` container-dimension layers off the front of a layered
// `detail` string (plan.md §6.15), returning what's left rejoined with
// ':' -- e.g. peeling 1 layer off "$fixed_array:$queue:MyClass" yields
// "$queue:MyClass" (arr[i] is still a queue, not yet a MyClass); peeling 2
// yields "MyClass" (arr[i][j] reaches the element). Only the *leading run*
// of recognized container-dimension tags counts as indexable -- an element
// layer (a bare class name, or $string/$event) is never itself peelable,
// so over-indexing (depth exceeding that leading run) fails closed by
// returning "", same posture as every other §6.14 resolution failure (see
// candidatesForResolvedType's own doc comment for why "" specifically means
// "nothing to offer" in this schema, never "the whole top-level scope").
// depth <= 0 is a no-op (returns `detail` unchanged) -- every non-indexed
// segment goes through this path too, at depth 0.
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

// Resolves a bare class name (as stored in ParseRecord::detail --
// userTypeName() deliberately never produces a qualified/"::"-containing
// name) to the fully-qualified scope chain its own members are actually
// stored under (plan.md §6.17) -- e.g. "PolicyImpl" -> "PolicyImpl"
// unchanged for a top-level class, or "PolicyImpl" -> "policy_pkg::PolicyImpl"
// for one declared inside a package (or nested inside another class, at
// any depth -- the found row's own `scope` column already carries
// whatever full chain applies). Resolves via the class's own DB row
// (found by name, disambiguated with the same pickBestSymbol()
// same-file-then-first-row logic hover/definition/super already use) --
// not by assuming `className` is already qualified, since it never is.
// Returns "" if `className` doesn't name any real Class row (fail closed,
// same posture as every other resolution failure in this file) -- this
// also naturally covers container/type tags ($queue, $queue:MyClass, ...)
// and a Function's raw built-in return-type text ("void", ...), neither of
// which can ever be a real symbol name, so findSymbolsByName below always
// comes back empty for them, same as before this fix existed.
std::string qualifiedClassScope(SymbolDatabase& db, const std::string& className,
                                 const std::string& curPath)
{
    std::vector<SymbolRow> classRows;
    for (auto& row : db.findSymbolsByName(className))
        if (row.kind == "Class") classRows.push_back(row);
    if (classRows.empty()) return "";
    const SymbolRow* best = pickBestSymbol(classRows, curPath);
    return best->scope.empty() ? best->name : best->scope + "::" + best->name;
}

// Builds the candidate list for whatever a dot-completion chain resolved
// to -- shared by every chain length (a 1-segment chain reproduces
// §6.10/§6.13's original single-hop behavior exactly; this is the only
// place that logic lives now). Guards against `detail.empty()` internally:
// "" is not "unknown type" in this schema, it's the literal top-level
// scope value every top-level symbol is stored under, so
// findSymbolsInScope("") would wrongly return the whole project's
// top-level symbols instead of nothing (plan.md §6.14's own documented
// landmine).
std::vector<Candidate> candidatesForResolvedType(SymbolDatabase& db, const std::string& detail,
                                                  const std::string& curPath)
{
    if (detail.empty()) return {};

    // Built-in container/type methods (plan.md §6.13, §6.15): queues,
    // associative/dynamic/fixed-size arrays, mailbox, semaphore, process,
    // string, event -- none of these are ParseRecordKinds, so
    // findSymbolsInScope would return nothing for them. Resolved from a
    // static table alone, no DB call. Dispatches on the outermost layer
    // only -- a container's own method set doesn't depend on its element
    // type (§6.15's "q." on "MyClass q[$]" still means the queue's own
    // methods, not MyClass's).
    if (auto methods = builtinMethodsFor(firstLayer(detail)); !methods.empty())
        return candidatesFromMethods(methods);

    // Otherwise, a real DB Class/Interface/whatever scope lookup --
    // qualified via qualifiedClassScope() (plan.md §6.17) so a class
    // declared inside a package (or nested inside another class) resolves
    // correctly, not just a top-level one. A non-empty result here already
    // proves `detail` names a genuine Class, so it doubles as the gate for
    // unioning in the randomize-family methods below -- no separate
    // findSymbolsByName scan needed for that anymore (this used to be two
    // independent lookups; qualifiedClassScope's own already does the one
    // that matters).
    const std::string qualified = qualifiedClassScope(db, detail, curPath);
    if (qualified.empty()) return {};

    auto memberRows = db.findSymbolsInScope(qualified);
    auto candidates = candidatesFromRows(memberRows);

    // Union the randomize-family methods the LRM implicitly grants every
    // class. A user class that declares its own `randomize` override
    // keeps the real (DB) one -- skip the synthetic entry on a name
    // collision rather than duplicating it.
    for (auto& m : RANDOMIZE_METHODS) {
        bool collides = std::any_of(memberRows.begin(), memberRows.end(),
            [&](const SymbolRow& row) { return row.name == m.name; });
        if (!collides)
            candidates.push_back({std::string(m.name), m.kind, std::string(m.detail)});
    }

    return candidates;
}

// Resolves a dot-completion chain's first segment against what's visible
// at the cursor (not as a member of anything -- that's resolveMemberSegment
// below). Returns "" on failure (fail closed).
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

// Resolves a non-first chain segment as a member of `prevClass`'s scope.
// Returns "" on failure (fail closed). Qualifies `prevClass` via
// qualifiedClassScope() (plan.md §6.17) the same way candidatesForResolvedType
// does -- an *intermediate* chain segment (e.g. the "child" in
// "obj.child.greet") can resolve to a package-nested class exactly as
// easily as the terminal one, and has the same bug independently if left
// unqualified.
std::string resolveMemberSegment(SymbolDatabase& db, const std::string& curPath,
                                  const std::string& prevClass, const ChainSegment& seg)
{
    if (prevClass.empty()) return "";
    const std::string qualified = qualifiedClassScope(db, prevClass, curPath);
    if (qualified.empty()) return "";
    auto members = db.findSymbolsInScope(qualified);
    const char* wantKind = seg.isCall ? "Function" : nullptr;
    for (auto& row : members) {
        bool kindMatches = wantKind ? row.kind == wantKind
                                     : (row.kind == "Signal" || row.kind == "Parameter");
        if (kindMatches && row.name == seg.name)
            return peelDimensionLayers(row.detail, seg.indexDepth);
    }
    return "";
}

// Resolves an entire dot-completion chain left to right to the type/scope
// name backing its final segment's members. Returns nullopt if any hop
// fails to resolve (fail closed) -- including a hop resolving to ""; see
// candidatesForResolvedType's own doc comment for why an empty scope name
// can never be treated as "no type" and passed through.
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

} // namespace

lsp::TextDocument_CompletionResult CompletionProvider::getCompletion(
    const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string path{params.textDocument.uri.path()};
    // LSP position is 0-based; scopeAtPosition uses 1-based lines.
    const int line1 = static_cast<int>(params.position.line) + 1;

    if (auto dot = dotCompletionContext(docText, params.position.line, params.position.character)) {
        auto resolved = resolveChain(db, path, line1, dot->segments);
        if (!resolved) return nullptr;
        return buildCompletionItems(candidatesForResolvedType(db, *resolved, path), dot->prefix);
    }

    const std::string prefix = wordAtPosition(docText,
                                              params.position.line,
                                              params.position.character);

    // Merge DB symbol rows with whatever SV keywords are legal at this
    // scope (plan.md §6.9) before scoring/emptiness-checking, so a file
    // with zero DB symbols (e.g. a brand-new buffer) still offers top-level
    // keywords instead of returning null.
    auto candidates = candidatesFromRows(db.findSymbolsVisibleAt(path, line1));
    auto keywords    = candidatesFromKeywords(db.scopeKindAtPosition(path, line1));
    candidates.insert(candidates.end(),
                       std::make_move_iterator(keywords.begin()),
                       std::make_move_iterator(keywords.end()));
    return buildCompletionItems(candidates, prefix);
}
