#include "lsp/fuzzy_match.h"
#include <cctype>

namespace {

char toLowerChar(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// True when `pos` starts a new "word" inside `s` — the very start of the
// string, right after a separator, or a lower/digit-to-upper camelCase
// transition. Matches landing on a boundary read as more intentional than
// matches landing mid-word, so they're scored higher.
bool isWordBoundary(const std::string& s, size_t pos)
{
    if (pos == 0) return true;

    const char prev = s[pos - 1];
    const char cur  = s[pos];

    if (prev == '_' || prev == '-' || prev == '.' || prev == ':')
        return true;

    if ((std::islower(static_cast<unsigned char>(prev)) ||
         std::isdigit(static_cast<unsigned char>(prev))) &&
        std::isupper(static_cast<unsigned char>(cur)))
        return true;

    return false;
}

} // namespace

std::optional<int> fuzzyScore(const std::string& candidate, const std::string& pattern)
{
    if (pattern.empty()) return 0;

    int  score     = 0;
    long lastMatch = -1; // index of the previously matched candidate character

    for (char pchar : pattern) {
        const char pc = toLowerChar(pchar);

        // Prefer continuing the previous match contiguously when possible —
        // this is what makes a solid run of characters outscore an
        // equally-long but scattered subsequence match.
        long matchPos = -1;
        const size_t contiguousCandidate = static_cast<size_t>(lastMatch + 1);
        if (lastMatch >= 0 && contiguousCandidate < candidate.size() &&
            toLowerChar(candidate[contiguousCandidate]) == pc) {
            matchPos = static_cast<long>(contiguousCandidate);
        } else {
            for (size_t i = static_cast<size_t>(lastMatch + 1); i < candidate.size(); ++i) {
                if (toLowerChar(candidate[i]) == pc) {
                    matchPos = static_cast<long>(i);
                    break;
                }
            }
        }

        if (matchPos < 0) return std::nullopt;

        score += 10; // every matched character counts for something

        if (lastMatch >= 0 && matchPos == lastMatch + 1)
            score += 15; // contiguous run bonus

        if (isWordBoundary(candidate, static_cast<size_t>(matchPos)))
            score += 12; // matched right at a word start / boundary

        if (candidate[static_cast<size_t>(matchPos)] == pchar)
            score += 3; // exact-case tiebreak

        // Penalize the characters skipped over to reach this match.
        score -= static_cast<int>(matchPos - (lastMatch + 1));

        lastMatch = matchPos;
    }

    // Mild preference for shorter overall candidates when match quality ties.
    score -= static_cast<int>(candidate.size()) / 10;

    return score;
}
