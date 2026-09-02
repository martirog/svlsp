#include "lsp/completion.h"
#include "lsp/symbol_utils.h"
#include "lsp/fuzzy_match.h"
#include "lsp/sv_keywords.h"
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

// buildCompletionItems overload for a single DB row source (used by the
// dot-completion branch, which never merges keywords -- there's no such
// thing as "foo.if").
lsp::TextDocument_CompletionResult buildCompletionItems(
    const std::vector<SymbolRow>& rows, const std::string& prefix)
{
    return buildCompletionItems(candidatesFromRows(rows), prefix);
}

} // namespace

lsp::TextDocument_CompletionResult CompletionProvider::getCompletion(
    const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string path{params.textDocument.uri.path()};
    // LSP position is 0-based; scopeAtPosition uses 1-based lines.
    const int line1 = static_cast<int>(params.position.line) + 1;

    if (auto dot = dotCompletionContext(docText, params.position.line, params.position.character)) {
        // Resolve `dot->object`'s declared type through whatever's visible
        // at this scope (respects the same scoping/precedence as ordinary
        // completion — the first exact-name match is the innermost-scoped
        // declaration, since findSymbolsVisibleAt is already sorted by scope
        // depth). Only Signal/Parameter carry a declared-type `detail`
        // (§6.10 first cut — see sv_tree_walker.cpp's userTypeName); any
        // other kind, or a type that itself isn't a known scope, fails to
        // resolve and returns no completions rather than falling back to
        // scope-wide completion (offering unrelated symbols after an
        // explicit '.' would be a worse result than offering nothing).
        auto visible = db.findSymbolsVisibleAt(path, line1);
        const SymbolRow* objRow = nullptr;
        for (auto& row : visible) {
            if ((row.kind == "Signal" || row.kind == "Parameter") && row.name == dot->object) {
                objRow = &row;
                break;
            }
        }
        if (!objRow || objRow->detail.empty()) return nullptr;

        return buildCompletionItems(db.findSymbolsInScope(objRow->detail), dot->prefix);
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
