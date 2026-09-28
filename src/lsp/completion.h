#pragma once

#include <lsp/messages.h>
#include "db/symbol_database.h"

// CompletionProvider handles textDocument/completion requests.
class CompletionProvider {
public:
    // `fuzzyEnabled` (plan.md §6.11, default true = today's unchanged
    // behavior): with a typed prefix, true fuzzy-scores/ranks candidates
    // (typo/skip-tolerant subsequence matching); false restores the
    // pre-fuzzy strict-prefix filter, DB order preserved, unranked.
    static lsp::TextDocument_CompletionResult getCompletion(
        const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText,
        bool fuzzyEnabled = true);

    // completionItem/resolve (plan.md §6.31): fills `documentation` from the
    // doc comment of the symbol the item's `data` names (path, line, col,
    // kind, scope -- set by getCompletion on every DB-symbol item). An item
    // without `data` (a keyword, a built-in method) or without a doc comes
    // back unchanged.
    static lsp::CompletionItem resolve(lsp::CompletionItem item, SymbolDatabase& db);
};
