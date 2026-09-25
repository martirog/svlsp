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

// Inverse of a position->offset conversion (each LSP feature provider that
// needs one keeps its own local `toOffset`, e.g. signature_help.cpp): converts
// a flat 0-based byte offset into `text` back to a 0-based (line, character)
// LSP position. Used by signature_help.cpp to re-enter dotCompletionContext
// at a call's own name (see resolveChain's own doc comment below) starting
// from a byte offset (`findEnclosingParen`'s own coordinate system) rather
// than a (line, character) pair. Clamps to the last valid position if
// `offset` is beyond `text`'s end, rather than asserting -- callers here
// always derive `offset` from a position already known to be valid, but
// there is no reason to crash on a hypothetical off-by-one instead of
// degrading gracefully.
lsp::Position positionForOffset(const std::string& text, size_t offset);

// Peels `depth` container-dimension layers off the front of a layered
// `detail` string (plan.md §6.15) -- see completion.cpp's original
// doc comment (unchanged, only relocated here in plan.md §6.27 so
// signature_help.cpp's dotted-call resolution can share it too) for the
// full rationale. depth <= 0 is a no-op.
std::string peelDimensionLayers(const std::string& detail, int depth);

// The outermost layer of a (possibly layered) detail string -- what
// container/element-tag dispatch (builtinMethodsFor) keys off. Splits on a
// single ':' only, so a `pkg::`-qualified type name (plan.md §6.30 step A)
// is one layer. A single-layer detail is returned unchanged.
std::string firstTypeLayer(const std::string& detail);

// Unions member rows across `className`'s own scope and every ancestor
// reachable via `extends` (plan.md §6.26 -- SymbolDatabase::baseClassChain),
// deduped by name so a derived class's own override always wins over an
// ancestor's same-named member. {} if `className` isn't a known class at
// all. Shared by completion.cpp's chain resolution and (plan.md §6.27)
// signature_help.cpp's dotted-call resolution.
std::vector<SymbolRow> membersAcrossChain(SymbolDatabase& db, const std::string& className,
                                           const std::string& curPath);

// Resolves a dot-completion chain's first segment against what's visible at
// the cursor (not as a member of anything -- that's resolveMemberSegment
// below). Returns "" on failure (fail closed). See completion.cpp's
// original doc comment for the full `this`/`super` design.
std::string resolveFirstSegment(SymbolDatabase& db, const std::string& path, int line1,
                                 const ChainSegment& seg);

// Resolves a non-first chain segment as a member of `prevClass`'s own class
// or one of its ancestors via `extends`. Returns "" on failure (fail closed).
std::string resolveMemberSegment(SymbolDatabase& db, const std::string& curPath,
                                  const std::string& prevClass, const ChainSegment& seg);

// Resolves an entire dot-completion chain left to right to the type/scope
// name backing its final segment's own members. Returns nullopt if any hop
// fails to resolve (fail closed). Shared by completion.cpp (plan.md §6.14)
// and, since plan.md §6.27, signature_help.cpp's dotted-call resolution --
// the latter passes `segments` *excluding* the final call name itself (the
// receiver chain only), so the result is the receiver's own type, not
// whatever the call itself might return.
std::optional<std::string> resolveChain(SymbolDatabase& db, const std::string& path, int line1,
                                         const std::vector<ChainSegment>& segments);

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

// True if the '(' at `parenPos` opens a named port/parameter connection's
// own parens (`.name(` where the '.' starts a fresh argument, i.e. follows
// '(' or ',' or the start of text), as opposed to a nested call like
// `obj.get(`. Moved here from signature_help.cpp for plan.md §6.30's
// resolver, which needs the same distinction.
bool isNamedConnectionParen(const std::string& text, size_t parenPos);

// The enclosing '(' of the argument list `offset` sits inside, walking
// backward with a paren-depth counter and skipping named connections' own
// parens (see isNamedConnectionParen). Not comment/string-aware.
std::optional<size_t> findEnclosingParen(const std::string& text, size_t offset);
