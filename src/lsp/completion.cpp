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

// Builds the candidate list for whatever a dot-completion chain resolved
// to -- shared by every chain length (a 1-segment chain reproduces
// §6.10/§6.13's original single-hop behavior exactly; this is the only
// place that logic lives now). Guards against `detail.empty()` internally:
// "" is not "unknown type" in this schema, it's the literal top-level
// scope value every top-level symbol is stored under, so
// findSymbolsInScope("") would wrongly return the whole project's
// top-level symbols instead of nothing (plan.md §6.14's own documented
// landmine).
std::vector<Candidate> candidatesForResolvedType(SymbolDatabase& db, const std::string& detail)
{
    if (detail.empty()) return {};

    // Built-in container/type methods (plan.md §6.13): queues,
    // associative/dynamic/fixed-size arrays, mailbox, semaphore, process,
    // string, event -- none of these are ParseRecordKinds, so
    // findSymbolsInScope would return nothing for them. Resolved from a
    // static table alone, no DB call.
    if (auto methods = builtinMethodsFor(detail); !methods.empty())
        return candidatesFromMethods(methods);

    // Otherwise, a real DB Class/Interface/whatever scope lookup.
    auto memberRows = db.findSymbolsInScope(detail);
    auto candidates = candidatesFromRows(memberRows);

    // Union the randomize-family methods the LRM implicitly grants every
    // class, but only when `detail` actually names a genuine user-declared
    // Class -- gating on that (not just "did findSymbolsInScope return
    // anything") keeps an unresolved/bogus type failing closed instead of
    // surfacing randomize() for a made-up name. A user class that declares
    // its own `randomize` override keeps the real (DB) one -- skip the
    // synthetic entry on a name collision rather than duplicating it.
    //
    // No hand-maintained built-in-type-keyword list is needed here to tell
    // a genuine class name apart from a function's raw, unfiltered return-
    // type text ("void", "int unsigned", ...): SV reserved words can never
    // be valid identifiers, so a raw built-in-type string can never
    // collide with a real Class row -- the DB lookup below already comes
    // back empty for those, with no false-positive risk (plan.md §6.14's
    // own documented simplification over its original sketch).
    bool isClass = false;
    for (auto& row : db.findSymbolsByName(detail)) {
        if (row.kind == "Class") { isClass = true; break; }
    }
    if (isClass) {
        for (auto& m : RANDOMIZE_METHODS) {
            bool collides = std::any_of(memberRows.begin(), memberRows.end(),
                [&](const SymbolRow& row) { return row.name == m.name; });
            if (!collides)
                candidates.push_back({std::string(m.name), m.kind, std::string(m.detail)});
        }
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
        if (kindMatches && row.name == seg.name) return row.detail;
    }
    return "";
}

// Resolves a non-first chain segment as a member of `prevClass`'s scope.
// Returns "" on failure (fail closed).
std::string resolveMemberSegment(SymbolDatabase& db, const std::string& prevClass,
                                  const ChainSegment& seg)
{
    if (prevClass.empty()) return "";
    auto members = db.findSymbolsInScope(prevClass);
    const char* wantKind = seg.isCall ? "Function" : nullptr;
    for (auto& row : members) {
        bool kindMatches = wantKind ? row.kind == wantKind
                                     : (row.kind == "Signal" || row.kind == "Parameter");
        if (kindMatches && row.name == seg.name) return row.detail;
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
        current = resolveMemberSegment(db, current, segments[i]);
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
        return buildCompletionItems(candidatesForResolvedType(db, *resolved), dot->prefix);
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
