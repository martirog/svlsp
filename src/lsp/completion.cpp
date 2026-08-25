#include "lsp/completion.h"
#include "lsp/symbol_utils.h"
#include "lsp/fuzzy_match.h"
#include <algorithm>
#include <cstdio>
#include <vector>

lsp::TextDocument_CompletionResult CompletionProvider::getCompletion(
    const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText)
{
    const std::string path{params.textDocument.uri.path()};
    // LSP position is 0-based; scopeAtPosition uses 1-based lines.
    const int line1 = static_cast<int>(params.position.line) + 1;

    // Extract any partial identifier the user has already typed.
    const std::string prefix = wordAtPosition(docText,
                                              params.position.line,
                                              params.position.character);

    auto rows = db.findSymbolsVisibleAt(path, line1);
    if (rows.empty()) return nullptr;

    // Fuzzy-filter (subsequence match, typo/skip tolerant) and rank by score
    // when the user has typed something; with no prefix every visible symbol
    // stays in, unranked, exactly as before.
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
