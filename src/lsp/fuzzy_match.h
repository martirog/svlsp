#pragma once
#include <optional>
#include <string>

// Case-insensitive subsequence fuzzy matcher used to rank completion
// candidates against a user-typed (possibly non-contiguous or mistyped)
// prefix.
//
// Returns std::nullopt when `pattern` is not a subsequence of `candidate`;
// otherwise returns a score where a higher value is a better match. An
// empty `pattern` always matches with score 0 (no ranking preference).
std::optional<int> fuzzyScore(const std::string& candidate, const std::string& pattern);
