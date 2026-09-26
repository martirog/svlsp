#pragma once
#include "db/symbol_database.h"
#include <optional>
#include <string>
#include <vector>

// Macro names in source text (plan.md §6.29 part A and its follow-up):
// shared by signature help, hover, go-to-definition, references and rename.
// Macros live in their own table (SymbolDatabase::findMacros), never in
// `symbols`, and a position is a macro name only by its text shape:
//   - an identifier directly after a backtick (`` `NAME ``), except the
//     compiler-directive keywords (`` `define ``, `` `include ``, ...);
//   - the name operand of `` `define ``/`` `undef ``/`` `ifdef ``/
//     `` `ifndef ``/`` `elsif `` (the first identifier after the directive).
// Comments and string literals are never macro names. Disclosed: a
// token-paste operand (`` a``NAME ``) reads as a macro use too, and a macro
// use inside a stringification (`` `"...`" ``) is inside a string.

// The macro name at 0-based (line, character) of `text` -- the cursor may be
// on the name or on its backtick. `line`/`character` in the result are the
// name's first character.
struct MacroNameAt {
    std::string name;
    unsigned    line;
    unsigned    character;
};
std::optional<MacroNameAt> macroNameAt(const std::string& text, unsigned line,
                                       unsigned character);

// Whether the identifier starting at `offset` of `blankedText` (text after
// blankCommentsAndStrings) is a macro name, by the rules above.
bool isMacroOccurrence(const std::string& blankedText, size_t offset);

// Which of a (possibly redefined) macro's definitions a use in `curPath` at
// 1-based `line1` sees: the last one at or before it in the same file, else
// the first from another file (path order -- which of several other files'
// definitions is in effect isn't tracked), else a same-file one after it.
std::optional<MacroRow> pickMacro(const std::vector<MacroRow>& rows, const std::string& curPath,
                                  int line1);

// `` `NAME(A, B = 1) `` for a function-like macro, `` `NAME `` otherwise.
std::string macroSignature(const MacroRow& macro);
