#pragma once
#include <lsp/messages.h>
#include <string>
#include <vector>
#include "db/symbol_database.h"

// Shared utilities used by LSP feature providers.

// Map a ParseRecordKind string ("Module", "Signal", …) to lsp::SymbolKind.
lsp::SymbolKind        symbolKindFor(const std::string& kind);

// Map a ParseRecordKind string to lsp::CompletionItemKind.
lsp::CompletionItemKind completionKindFor(const std::string& kind);

// Extract the identifier token at (line, character) in text.
// line and character are 0-based (LSP convention).
// Returns "" when the position is out of range or not on an identifier character.
std::string wordAtPosition(const std::string& text, unsigned line, unsigned character);

// Build an LSP Range spanning one identifier.
// line1 is 1-based (ParseRecord convention); col0 is 0-based.
lsp::Range makeRange(int line1, int col0, int nameLen);

// Build a file:// DocumentUri from an absolute filesystem path.
lsp::DocumentUri pathToUri(const std::string& path);

// Pick the best match among same-named symbols for hover/definition, given the
// path of the file the request originated in. `rows` must be non-empty.
// 1. A same-file match always wins (existing behavior).
// 2. Otherwise, prefer a "declaration-like" kind (Module/Interface/Program/
//    Package/Class/Function/Task) over a "data-like" one (Signal/Port/
//    Parameter/Macro) — defense-in-depth against `findSymbolsByName`'s
//    alphabetical-by-path tiebreak picking an unrelated same-named variable
//    over the intended definition on an unlucky name collision.
// 3. Otherwise, the first row (caller's existing path,line order).
const SymbolRow* pickBestSymbol(const std::vector<SymbolRow>& rows, const std::string& curPath);
