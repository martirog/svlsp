#pragma once
#include <lsp/messages.h>
#include <optional>
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

// One segment of a dot-completion chain: a bare identifier ("foo"), a call
// ("get_child(...)" -- argument text is never captured, only the identifier
// and the fact that it was called matter for resolution), or an indexed
// access ("arr[i][j]" -- indexDepth counts the consecutive bracket groups,
// 2 here; index expression text is never captured either, only how many
// there are, plan.md §6.15). isCall and indexDepth > 0 never both apply to
// the same segment (indexing directly off a call's result is out of scope
// for §6.15 -- see dotCompletionContext's own doc comment).
struct ChainSegment {
    std::string name;
    bool        isCall;
    int         indexDepth = 0;
};

// A dot/member-access completion context: `segments` is the chain of
// identifiers/calls before the triggering '.' (segments[0] is leftmost —
// e.g. "foo.bar." is [{"foo",false},{"bar",false}]), `prefix` is whatever
// partial member name has been typed after the last '.' (possibly empty,
// e.g. right after `foo.`). A single-identifier chain like `foo.` is just
// a 1-segment case, not a separate concept.
struct DotCompletion {
    std::vector<ChainSegment> segments;
    std::string               prefix;
};

// Detects a dot-completion context at (line, character): a chain of
// identifiers, calls, and/or indexed accesses immediately preceded by a '.'
// (e.g. "foo.b|" -> segments=[{"foo",false,0}], prefix="b"; "a().b().c|" ->
// segments=[{"a",true,0},{"b",true,0}], prefix="c"; "arr[i][j].m|" ->
// segments=[{"arr",false,2}], prefix="m" -- one segment, not two, since
// there's no '.' between the bracket groups). Returns std::nullopt when the
// cursor isn't in one: no '.' immediately before the prefix identifier, or
// nothing identifier/call/index-like before some '.' in the chain either (a
// bare "." with no object).
//
// Walks left one segment at a time: a segment ending in ')' is read as a
// call by paren-balance-matching back to the '(' (nested calls and
// string-literal arguments containing '.'/'(' /')' are handled -- the
// balance walk tracks a minimal in-string state so `foo("a.b").c` isn't
// confused by the dot inside the string), then reads the identifier
// immediately before that '('; a segment ending in ']' is read as an
// indexed access the same way, bracket-matching back to '[' and repeating
// for each further consecutive ']' immediately to its left (same in-string
// tracking, so `aa["a.b"].c` isn't confused either), then reads the
// identifier immediately before the leftmost '['; anything else reads as a
// plain identifier. A segment ending in ')' preceded by a further ']', or
// vice versa (indexing a call's own result), is not supported -- fails
// closed the same as any other unrecognized shape, plan.md §6.15 leaves
// this as a distinct follow-up. Each segment's own left edge is checked for
// a preceding '.' to decide whether to continue the chain leftward or stop
// (segment 0 reached). Whitespace before a call's own '(' is tolerated;
// whitespace around the chain's '.'s is not (matches this function's
// pre-existing convention).
std::optional<DotCompletion> dotCompletionContext(
    const std::string& text, unsigned line, unsigned character);

// A lexical hit for a whole-identifier text search (findIdentifierOccurrences
// below) -- 0-based LSP line/character of the occurrence's first character.
struct TextOccurrence {
    int line;
    int character;
};

// Every whole-identifier occurrence of `name` in `text` -- used by
// ReferencesProvider/RenameProvider (there is no reference-tracking table,
// only declarations, so both fall back to this). A lexical scan only: skips
// `//` and `/* */` comments and `"..."` string literals (same in-string
// escape handling as the preprocessor's own macro-argument scanning) so a
// same-named identifier inside one of those is never reported, but it is
// NOT scope-aware -- two unrelated declarations sharing a name (e.g. two
// classes both named `Packet` in different files) are indistinguishable
// from here and both match. `name` must be a plain identifier (no `::`/`.`);
// an empty `name` returns {}.
std::vector<TextOccurrence> findIdentifierOccurrences(
    const std::string& text, const std::string& name);
