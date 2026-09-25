#pragma once
#include "db/symbol_database.h"
#include <optional>
#include <string>

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
//   3. `recv.name` -- resolve the receiver chain to its declared class
//      (types are resolved in the context of *their own* declaration, so an
//      imported or `pkg::`-qualified type means what it meant there), then
//      find `name` among that class's and its ancestors' members.
//   4. a bare `name` -- innermost lexical scope first; inside a class, the
//      class's inherited members at the class's own level; then a specific
//      import, then wildcard-imported packages, then other files' top level.
//      A name followed by `::` only matches a Package or Class.
//
// `exact` is false when the qualifier/receiver/name couldn't be understood
// at all (an unknown package, a struct or hierarchical-instance receiver, a
// name nothing visible declares) and the result is the old name-only
// fallback -- kept so those shapes don't regress. When the qualifier or
// receiver *is* understood but has no such member, the result is nullopt
// rather than a guess (plan.md §6.26's own rule).
//
// Disclosed limits (plan.md §6.30): begin/end and generate blocks are not
// scopes; scope containment is line-granular; out-of-class method bodies
// are not yet nested under their class (step D).
struct ResolvedSymbol {
    SymbolRow row;
    bool      exact;
};

std::optional<ResolvedSymbol> resolveSymbolAt(SymbolDatabase& db, const std::string& path,
                                              const std::string& text, unsigned line,
                                              unsigned character);
