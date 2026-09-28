#pragma once
#include <string>
#include <vector>

// Doc comments on declarations (plan.md §6.31).
//
// Turns the raw text of a comment block -- one entry per comment, in source
// order, each a `// ...` line comment (its newline may still be attached) or
// a `/* ... */` block comment -- into the docstring shown by hover, signature
// help and completion: comment markers (`//`, `///`, `/*`, `/**`, `*/`, a
// block line's leading `*`) stripped, separator lines (`//-----`, `//=====`)
// and tag lines (`@uvm-ieee ...`) dropped, common indentation removed, line
// breaks kept, leading/trailing empty lines trimmed. "" when nothing is left.
std::string cleanDocComment(const std::vector<std::string>& comments);
