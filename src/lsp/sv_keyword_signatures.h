#pragma once
#include <string_view>

// Signature help for keyword constructs with a parenthesized header (plan.md
// §6.29 part B). Only headers with more than one part, or whose operand
// isn't obvious, are listed: `if`/`while`/`repeat`/`wait (` take a single
// obvious expression and stay null.
//
// Disclosed uncertainty: header shapes are reconstructed from training-time
// familiarity with IEEE 1800-2017, not verified against an LRM copy -- same
// posture as sv_system_tasks.h and sv_keywords.h.

// Rendering: `for (a; b; c)`, `foreach (array[index, ...])`,
// `<prefix> <keyword> (p)` otherwise, and `randomize(p)` with no space
// (it reads as a call).
enum class KeywordSeparator {
    None,      // a single parameter
    Semicolon, // `for`: top-level `;` separates the parameters
    Bracket,   // `foreach`: the second parameter starts at a top-level `[`
    Comma,     // a single variadic parameter list (`randomize`)
};

struct KeywordSignature {
    std::string_view keyword; // the identifier directly before `(`
    std::string_view prefix;  // keyword required directly before `keyword`, or ""
    std::string_view params;  // "; "-separated parameter labels
    KeywordSeparator separator;
    std::string_view doc;
};

inline constexpr KeywordSignature KEYWORD_SIGNATURES[] = {
    {"for", "", "initialization; condition; step", KeywordSeparator::Semicolon,
     "Loop: initialization runs once, the body runs while condition holds, step runs after each iteration."},
    {"foreach", "", "array; index, ...", KeywordSeparator::Bracket,
     "Iterate over every element of an array; one index variable per dimension (empty positions skip a dimension)."},
    {"case",  "", "expression", KeywordSeparator::None, "Case statement: items compared with ===."},
    {"casez", "", "expression", KeywordSeparator::None, "Case statement: z and ? bits are don't-cares."},
    {"casex", "", "expression", KeywordSeparator::None, "Case statement: x, z and ? bits are don't-cares."},

    // Immediate assertions: `assert (expression) [action] [else action]`.
    {"assert", "", "expression", KeywordSeparator::None, "Immediate assertion; optional pass action and else action."},
    {"assume", "", "expression", KeywordSeparator::None, "Immediate assumption; optional pass action and else action."},
    {"cover",  "", "expression", KeywordSeparator::None, "Immediate cover; optional pass statement."},
    // Deferred immediate assertions (`#0` form not handled: not an identifier).
    {"final", "assert", "expression", KeywordSeparator::None, "Final deferred assertion, evaluated at the end of the time step."},
    {"final", "assume", "expression", KeywordSeparator::None, "Final deferred assumption, evaluated at the end of the time step."},
    {"final", "cover",  "expression", KeywordSeparator::None, "Final deferred cover, evaluated at the end of the time step."},
    // Concurrent assertions.
    {"property", "assert",   "property_spec", KeywordSeparator::None, "Concurrent assertion: [clocking_event] [disable iff (expr)] property_expr."},
    {"property", "assume",   "property_spec", KeywordSeparator::None, "Concurrent assumption: [clocking_event] [disable iff (expr)] property_expr."},
    {"property", "cover",    "property_spec", KeywordSeparator::None, "Concurrent cover: [clocking_event] [disable iff (expr)] property_expr."},
    {"property", "restrict", "property_spec", KeywordSeparator::None, "Formal-verification constraint; ignored in simulation."},
    {"sequence", "cover",    "sequence_expr", KeywordSeparator::None, "Cover every match of a sequence: [clocking_event] [disable iff (expr)] sequence_expr."},
    {"expect", "", "property_spec", KeywordSeparator::None, "Block until the property succeeds or fails; optional pass and else actions."},
    {"iff", "disable", "expression", KeywordSeparator::None, "Disable condition: the property is not evaluated while expression is true."},

    // Built-in randomization (can't be user-declared): `obj.randomize(`,
    // bare `randomize(` inside a class, and `std::randomize(`.
    {"randomize", "", "[variable, ...]", KeywordSeparator::Comma,
     "Randomize the object's rand variables, or only the listed ones; returns 1 on success. Follow with `with { ... }` for inline constraints."},
};
