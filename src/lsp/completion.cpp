#include "lsp/completion.h"
#include "lsp/symbol_utils.h"
#include "lsp/fuzzy_match.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

// Fuzzy-filters (subsequence match, typo/skip tolerant) and ranks `rows`
// against `prefix`, then builds the LSP item list. With no prefix every row
// stays in, unranked. Returns nullptr if `rows` is empty, or if every row
// fails to match a non-empty prefix.
lsp::TextDocument_CompletionResult buildCompletionItems(
    const std::vector<SymbolRow>& rows, const std::string& prefix)
{
    if (rows.empty()) return nullptr;

    struct Scored { const SymbolRow* row; int score; };
    std::vector<Scored> scored;
    scored.reserve(rows.size());
    for (auto& row : rows) {
        int score = 0;
        if (!prefix.empty()) {
            auto s = fuzzyScore(row.name, prefix);
            if (!s) continue;
            score = *s;
        }
        scored.push_back({&row, score});
    }
    if (scored.empty()) return nullptr;

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.row->name < b.row->name;
    });

    lsp::Array<lsp::CompletionItem> items;
    items.reserve(scored.size());
    for (size_t i = 0; i < scored.size(); ++i) {
        const auto& row = *scored[i].row;

        lsp::CompletionItem item;
        item.label = row.name;
        item.kind  = completionKindFor(row.kind);
        if (!row.detail.empty())
            item.detail = row.detail;
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
    return buildCompletionItems(db.findSymbolsVisibleAt(path, line1), prefix);
}
