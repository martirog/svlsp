#pragma once
#include "compiler/parse_record.h"
#include "db/symbol_database.h"
#include "lsp/symbol_utils.h"
#include <optional>
#include <string>
#include <vector>
#include <utility>

// Scope-aware resolution of the identifier under the cursor to the single
// declaration it refers to (plan.md §6.30 step A). Shared by hover,
// go-to-definition, references and rename (step B), replacing their
// name-only findSymbolsByName + pickBestSymbol lookup.
//
// Shapes, tried in order:
//   1. `A::B::name` -- walk the qualifier (package, class, nested class,
//      `$unit`), then find `name` in that scope (a class scope includes its
//      `extends` ancestors).
//   2. `.name` preceded by '(' or ',' -- a named port / parameter / argument
//      connection: `name` is a Port or Parameter of the instantiated module
//      (or of the called function/task), not a member of anything.
//   3. `recv.name` -- resolve the receiver chain to the row owning its
//      members (see ReceiverType; types are resolved in the context of
//      *their own* declaration, so an imported or `pkg::`-qualified type
//      means what it meant there), then find `name` among them -- for a
//      class, among its and its ancestors' members.
//   4. a bare `name` -- innermost lexical scope first; inside a class, the
//      class's inherited members at the class's own level; then a specific
//      import, then wildcard-imported packages, then other files' top level.
//      A name followed by `::` only matches a Package or Class.
//
// `exact` is false when the qualifier/receiver/name couldn't be understood
// at all (an unknown package, a receiver of unknown type or a container, a
// name nothing visible declares) and the result is the old name-only
// fallback -- kept so those shapes don't regress. When the qualifier or
// receiver *is* understood but has no such member, the result is nullopt
// rather than a guess (plan.md §6.26's own rule).
struct ResolvedSymbol {
    SymbolRow row;
    bool      exact;
};

std::optional<ResolvedSymbol> resolveSymbolAt(SymbolDatabase& db, const std::string& path,
                                              const std::string& text, unsigned line,
                                              unsigned character);

// resolveSymbolAt for many positions in one file, sharing a single
// comment/string-blanked copy of `text` -- references and rename resolve
// every lexical hit of a name. Each position is a 0-based (line,
// character); result[i] is the resolution of positions[i].
std::vector<std::optional<ResolvedSymbol>> resolveSymbolsAt(
    SymbolDatabase& db, const std::string& path, const std::string& text,
    const std::vector<std::pair<unsigned, unsigned>>& positions);

// The Function/Task each of `calls` (all in `path`, whose source is `text`)
// invokes, resolved at the callee name as resolveSymbolsAt resolves it;
// result[i] is calls[i]'s. A `::`-qualified call fails closed unless that
// is exact (plan.md §6.26); an unqualified one nothing visible declares
// falls back to a name-only Function/Task search with a same-file
// tie-break. nullopt also for a call whose name isn't at its recorded
// position in `text` (one expanded from a macro). This is
// CompilationController's CalleeResolver for the missing-argument check
// (plan.md §6.23).
std::vector<std::optional<SymbolRow>> resolveCallees(SymbolDatabase& db, const std::string& path,
                                                     const std::string& text,
                                                     const std::vector<CallRecord>& calls);

// What a dot receiver's value is. `owner` is the row whose qualified scope
// holds its members: a Class (members include its ancestors'), a struct/
// union Typedef or a variable/member declared with an inline struct, an
// Interface/Module/Program (an instance, a virtual interface, an interface
// port, or a design unit named from the top), or a named Block. Otherwise
// `builtin` is the remaining type text: a container (`$queue:C`), a
// built-in (`$string`, `mailbox`) or a type name that resolves to nothing.
struct ReceiverType {
    std::optional<SymbolRow> owner;
    std::string              builtin;
};

// The type of the receiver chain `segments` (dotCompletionContext's) at
// `at` (1-based line) in `path`, whose source is `text` -- what dot
// completion offers members of (plan.md §6.30, "Follow-up: struct,
// interface and hierarchical receivers"). nullopt when a segment resolves
// to nothing.
std::optional<ReceiverType> resolveReceiverType(SymbolDatabase& db, const std::string& path,
                                                const std::string& text,
                                                const std::vector<ChainSegment>& segments,
                                                SrcPos at);

// Identifies `row`'s override family: for a class method (Function/Task
// scoped to a class), the id of the same-named Function/Task declared by
// the topmost `extends` ancestor that declares one (the method's own id if
// no ancestor does); for anything else, `row.id`. Two rows with equal ids
// are the same symbol for references and rename -- a virtual method, its
// overrides and calls dispatched to either must be renamed together.
int64_t overrideFamilyId(SymbolDatabase& db, const SymbolRow& row);
