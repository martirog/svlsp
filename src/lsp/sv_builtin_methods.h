#pragma once
#include "compiler/parse_record.h"
#include <lsp/messages.h>
#include <span>
#include <string>
#include <string_view>

// Built-in method tables for SystemVerilog types with implicit,
// language-defined methods but no user-written declaration (plan.md §6.13):
// queues, associative arrays, dynamic/fixed-size unpacked arrays, mailbox,
// semaphore, process, string, event, and the randomization-family methods
// every class implicitly gains.
//
// Disclosed uncertainty: every method name/signature below is reconstructed
// from training-time familiarity with IEEE 1800-2017, not verified against
// an LRM copy (this server has no access to one) -- same posture already
// established for this feature area (see plan.md §6.13's own text and
// §6.9's checker-nesting caveat).
//
// Deliberate omission, repeated across every table: `new(...)` constructors
// are never listed. Dot-completion is member access on an *existing*
// instance (`mbx.`); `new` constructs one and is never invoked through a
// preceding dot on an instance, so listing it would be actively wrong, not
// just incomplete.

struct BuiltinMethod {
    std::string_view        name;
    std::string_view        detail; // short signature, e.g. "push_back(item)"
    lsp::CompletionItemKind kind;   // Method for calls, Property for a bare member
};

// LRM §7.12 locator/ordering/reduction methods, shared by every unpacked
// array kind except associative arrays (whose index isn't linearly ordered,
// so only the reduction subset applies -- see ASSOC_ARRAY_METHODS below).
inline constexpr BuiltinMethod ARRAY_LOCATOR_ORDERING_REDUCTION[] = {
    {"find",             "find(expr) with (...)",       lsp::CompletionItemKind::Method},
    {"find_index",       "find_index(expr) with (...)", lsp::CompletionItemKind::Method},
    {"find_first",       "find_first() with (...)",     lsp::CompletionItemKind::Method},
    {"find_first_index", "find_first_index() with (...)", lsp::CompletionItemKind::Method},
    {"find_last",        "find_last() with (...)",      lsp::CompletionItemKind::Method},
    {"find_last_index",  "find_last_index() with (...)", lsp::CompletionItemKind::Method},
    {"min",              "min()",                       lsp::CompletionItemKind::Method},
    {"max",              "max()",                       lsp::CompletionItemKind::Method},
    {"unique",           "unique()",                    lsp::CompletionItemKind::Method},
    {"unique_index",     "unique_index()",               lsp::CompletionItemKind::Method},
    {"reverse",          "reverse()",                   lsp::CompletionItemKind::Method},
    {"sort",             "sort()",                      lsp::CompletionItemKind::Method},
    {"rsort",            "rsort()",                     lsp::CompletionItemKind::Method},
    {"shuffle",          "shuffle()",                   lsp::CompletionItemKind::Method},
    {"sum",              "sum()",                       lsp::CompletionItemKind::Method},
    {"product",          "product()",                   lsp::CompletionItemKind::Method},
    {"and",              "and()",                       lsp::CompletionItemKind::Method},
    {"or",               "or()",                        lsp::CompletionItemKind::Method},
    {"xor",              "xor()",                       lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod FIXED_ARRAY_METHODS[] = {
    {"size", "size()", lsp::CompletionItemKind::Method},
    {"find",             "find(expr) with (...)",       lsp::CompletionItemKind::Method},
    {"find_index",       "find_index(expr) with (...)", lsp::CompletionItemKind::Method},
    {"find_first",       "find_first() with (...)",     lsp::CompletionItemKind::Method},
    {"find_first_index", "find_first_index() with (...)", lsp::CompletionItemKind::Method},
    {"find_last",        "find_last() with (...)",      lsp::CompletionItemKind::Method},
    {"find_last_index",  "find_last_index() with (...)", lsp::CompletionItemKind::Method},
    {"min",              "min()",                       lsp::CompletionItemKind::Method},
    {"max",              "max()",                       lsp::CompletionItemKind::Method},
    {"unique",           "unique()",                    lsp::CompletionItemKind::Method},
    {"unique_index",     "unique_index()",               lsp::CompletionItemKind::Method},
    {"reverse",          "reverse()",                   lsp::CompletionItemKind::Method},
    {"sort",             "sort()",                      lsp::CompletionItemKind::Method},
    {"rsort",            "rsort()",                     lsp::CompletionItemKind::Method},
    {"shuffle",          "shuffle()",                   lsp::CompletionItemKind::Method},
    {"sum",              "sum()",                       lsp::CompletionItemKind::Method},
    {"product",          "product()",                   lsp::CompletionItemKind::Method},
    {"and",              "and()",                       lsp::CompletionItemKind::Method},
    {"or",               "or()",                        lsp::CompletionItemKind::Method},
    {"xor",              "xor()",                       lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod DYNAMIC_ARRAY_METHODS[] = {
    {"size",   "size()",   lsp::CompletionItemKind::Method},
    {"delete", "delete()", lsp::CompletionItemKind::Method},
    {"find",             "find(expr) with (...)",       lsp::CompletionItemKind::Method},
    {"find_index",       "find_index(expr) with (...)", lsp::CompletionItemKind::Method},
    {"find_first",       "find_first() with (...)",     lsp::CompletionItemKind::Method},
    {"find_first_index", "find_first_index() with (...)", lsp::CompletionItemKind::Method},
    {"find_last",        "find_last() with (...)",      lsp::CompletionItemKind::Method},
    {"find_last_index",  "find_last_index() with (...)", lsp::CompletionItemKind::Method},
    {"min",              "min()",                       lsp::CompletionItemKind::Method},
    {"max",              "max()",                       lsp::CompletionItemKind::Method},
    {"unique",           "unique()",                    lsp::CompletionItemKind::Method},
    {"unique_index",     "unique_index()",               lsp::CompletionItemKind::Method},
    {"reverse",          "reverse()",                   lsp::CompletionItemKind::Method},
    {"sort",             "sort()",                      lsp::CompletionItemKind::Method},
    {"rsort",            "rsort()",                     lsp::CompletionItemKind::Method},
    {"shuffle",          "shuffle()",                   lsp::CompletionItemKind::Method},
    {"sum",              "sum()",                       lsp::CompletionItemKind::Method},
    {"product",          "product()",                   lsp::CompletionItemKind::Method},
    {"and",              "and()",                       lsp::CompletionItemKind::Method},
    {"or",               "or()",                        lsp::CompletionItemKind::Method},
    {"xor",              "xor()",                       lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod QUEUE_METHODS[] = {
    {"size",       "size()",             lsp::CompletionItemKind::Method},
    {"insert",     "insert(index, item)", lsp::CompletionItemKind::Method},
    {"delete",     "delete(index)",      lsp::CompletionItemKind::Method},
    {"pop_front",  "pop_front()",        lsp::CompletionItemKind::Method},
    {"pop_back",   "pop_back()",         lsp::CompletionItemKind::Method},
    {"push_front", "push_front(item)",   lsp::CompletionItemKind::Method},
    {"push_back",  "push_back(item)",    lsp::CompletionItemKind::Method},
    {"find",             "find(expr) with (...)",       lsp::CompletionItemKind::Method},
    {"find_index",       "find_index(expr) with (...)", lsp::CompletionItemKind::Method},
    {"find_first",       "find_first() with (...)",     lsp::CompletionItemKind::Method},
    {"find_first_index", "find_first_index() with (...)", lsp::CompletionItemKind::Method},
    {"find_last",        "find_last() with (...)",      lsp::CompletionItemKind::Method},
    {"find_last_index",  "find_last_index() with (...)", lsp::CompletionItemKind::Method},
    {"min",              "min()",                       lsp::CompletionItemKind::Method},
    {"max",              "max()",                       lsp::CompletionItemKind::Method},
    {"unique",           "unique()",                    lsp::CompletionItemKind::Method},
    {"unique_index",     "unique_index()",               lsp::CompletionItemKind::Method},
    {"reverse",          "reverse()",                   lsp::CompletionItemKind::Method},
    {"sort",             "sort()",                      lsp::CompletionItemKind::Method},
    {"rsort",            "rsort()",                     lsp::CompletionItemKind::Method},
    {"shuffle",          "shuffle()",                   lsp::CompletionItemKind::Method},
    {"sum",              "sum()",                       lsp::CompletionItemKind::Method},
    {"product",          "product()",                   lsp::CompletionItemKind::Method},
    {"and",              "and()",                       lsp::CompletionItemKind::Method},
    {"or",               "or()",                        lsp::CompletionItemKind::Method},
    {"xor",              "xor()",                       lsp::CompletionItemKind::Method},
};

// No locator/ordering methods (find/sort/reverse/...) -- an associative
// array's index isn't linearly ordered unless the index type itself is, so
// those don't carry over the way they do for queues/arrays.
inline constexpr BuiltinMethod ASSOC_ARRAY_METHODS[] = {
    {"num",    "num()",           lsp::CompletionItemKind::Method},
    {"delete", "delete(index)",   lsp::CompletionItemKind::Method},
    {"exists", "exists(index)",   lsp::CompletionItemKind::Method},
    {"first",  "first(ref index)", lsp::CompletionItemKind::Method},
    {"last",   "last(ref index)", lsp::CompletionItemKind::Method},
    {"next",   "next(ref index)", lsp::CompletionItemKind::Method},
    {"prev",   "prev(ref index)", lsp::CompletionItemKind::Method},
    {"sum",     "sum()",          lsp::CompletionItemKind::Method},
    {"product", "product()",      lsp::CompletionItemKind::Method},
    {"and",     "and()",          lsp::CompletionItemKind::Method},
    {"or",      "or()",           lsp::CompletionItemKind::Method},
    {"xor",     "xor()",          lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod MAILBOX_METHODS[] = {
    {"put",      "put(message)",      lsp::CompletionItemKind::Method},
    {"try_put",  "try_put(message)",  lsp::CompletionItemKind::Method},
    {"get",      "get(ref message)",  lsp::CompletionItemKind::Method},
    {"try_get",  "try_get(ref message)", lsp::CompletionItemKind::Method},
    {"peek",     "peek(ref message)", lsp::CompletionItemKind::Method},
    {"try_peek", "try_peek(ref message)", lsp::CompletionItemKind::Method},
    {"num",      "num()",             lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod SEMAPHORE_METHODS[] = {
    {"put",     "put(keyCount = 1)",     lsp::CompletionItemKind::Method},
    {"get",     "get(keyCount = 1)",     lsp::CompletionItemKind::Method},
    {"try_get", "try_get(keyCount = 1)", lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod PROCESS_METHODS[] = {
    {"status",        "status()",              lsp::CompletionItemKind::Method},
    {"self",          "self()",                lsp::CompletionItemKind::Method},
    {"kill",          "kill()",                lsp::CompletionItemKind::Method},
    {"await",         "await()",               lsp::CompletionItemKind::Method},
    {"suspend",       "suspend()",             lsp::CompletionItemKind::Method},
    {"resume",        "resume()",              lsp::CompletionItemKind::Method},
    {"srandom",       "srandom(seed)",         lsp::CompletionItemKind::Method},
    {"get_randstate", "get_randstate()",       lsp::CompletionItemKind::Method},
    {"set_randstate", "set_randstate(state)",  lsp::CompletionItemKind::Method},
};

// Unioned onto a genuine user-declared Class scope only (never a standalone
// container/type lookup) -- see CompletionProvider::getCompletion.
inline constexpr BuiltinMethod RANDOMIZE_METHODS[] = {
    {"randomize",       "randomize()",           lsp::CompletionItemKind::Method},
    {"pre_randomize",   "pre_randomize()",       lsp::CompletionItemKind::Method},
    {"post_randomize",  "post_randomize()",      lsp::CompletionItemKind::Method},
    {"srandom",         "srandom(seed)",         lsp::CompletionItemKind::Method},
    {"get_randstate",   "get_randstate()",       lsp::CompletionItemKind::Method},
    {"set_randstate",   "set_randstate(state)",  lsp::CompletionItemKind::Method},
    {"rand_mode",       "rand_mode(on_off)",     lsp::CompletionItemKind::Method},
    {"constraint_mode", "constraint_mode(on_off)", lsp::CompletionItemKind::Method},
};

inline constexpr BuiltinMethod STRING_METHODS[] = {
    {"len",      "len()",             lsp::CompletionItemKind::Method},
    {"putc",     "putc(i, c)",        lsp::CompletionItemKind::Method},
    {"getc",     "getc(i)",           lsp::CompletionItemKind::Method},
    {"toupper",  "toupper()",         lsp::CompletionItemKind::Method},
    {"tolower",  "tolower()",         lsp::CompletionItemKind::Method},
    {"compare",  "compare(s)",        lsp::CompletionItemKind::Method},
    {"icompare", "icompare(s)",       lsp::CompletionItemKind::Method},
    {"substr",   "substr(i, j)",      lsp::CompletionItemKind::Method},
    {"atoi",     "atoi()",            lsp::CompletionItemKind::Method},
    {"atohex",   "atohex()",          lsp::CompletionItemKind::Method},
    {"atooct",   "atooct()",          lsp::CompletionItemKind::Method},
    {"atobin",   "atobin()",          lsp::CompletionItemKind::Method},
    {"atoreal",  "atoreal()",         lsp::CompletionItemKind::Method},
    {"itoa",     "itoa(i)",           lsp::CompletionItemKind::Method},
    {"hextoa",   "hextoa(i)",         lsp::CompletionItemKind::Method},
    {"octtoa",   "octtoa(i)",         lsp::CompletionItemKind::Method},
    {"bintoa",   "bintoa(i)",         lsp::CompletionItemKind::Method},
    {"realtoa",  "realtoa(r)",        lsp::CompletionItemKind::Method},
};

// A bare built-in member, not a call -- LRM 15.5.1's event.triggered.
inline constexpr BuiltinMethod EVENT_MEMBERS[] = {
    {"triggered", "triggered", lsp::CompletionItemKind::Property},
};

// Returns the built-in method table for `detail` -- either one of
// parse_record.h's $-tagged container/type markers, or a literal built-in
// class name (mailbox/semaphore/process, which use userTypeName()'s
// ordinary class_type extraction verbatim, no tag needed). Empty span for
// anything else (an ordinary user-declared class/interface name, or a
// built-in scalar type with no member scope) -- caller falls back to a real
// DB Class/Interface lookup via findSymbolsInScope.
inline std::span<const BuiltinMethod> builtinMethodsFor(const std::string& detail)
{
    if (detail == CONTAINER_QUEUE)         return QUEUE_METHODS;
    if (detail == CONTAINER_ASSOC)         return ASSOC_ARRAY_METHODS;
    if (detail == CONTAINER_DYNAMIC_ARRAY) return DYNAMIC_ARRAY_METHODS;
    if (detail == CONTAINER_FIXED_ARRAY)   return FIXED_ARRAY_METHODS;
    if (detail == CONTAINER_STRING)        return STRING_METHODS;
    if (detail == CONTAINER_EVENT)         return EVENT_MEMBERS;
    if (detail == "mailbox")               return MAILBOX_METHODS;
    if (detail == "semaphore")             return SEMAPHORE_METHODS;
    if (detail == "process")               return PROCESS_METHODS;
    return {};
}
