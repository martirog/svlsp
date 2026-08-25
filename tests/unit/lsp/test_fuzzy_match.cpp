#include <catch2/catch_test_macros.hpp>
#include "lsp/fuzzy_match.h"

TEST_CASE("fuzzyScore: empty pattern always matches with score 0", "[fuzzy]")
{
    auto s = fuzzyScore("anything", "");
    REQUIRE(s.has_value());
    CHECK(*s == 0);
}

TEST_CASE("fuzzyScore: non-subsequence returns nullopt", "[fuzzy]")
{
    CHECK_FALSE(fuzzyScore("adder", "W").has_value());
    CHECK_FALSE(fuzzyScore("clk", "W").has_value());
    CHECK_FALSE(fuzzyScore("uvm_object", "objx").has_value()); // 'x' not present
}

TEST_CASE("fuzzyScore: exact prefix match succeeds and scores well", "[fuzzy]")
{
    auto s = fuzzyScore("WIDTH", "W");
    REQUIRE(s.has_value());
    CHECK(*s > 0);
}

TEST_CASE("fuzzyScore: contiguous prefix outranks a scattered subsequence", "[fuzzy]")
{
    // "uvm" is a contiguous prefix of uvm_object; "uo" is a scattered
    // (skip-heavy) subsequence match of the same candidate.
    auto prefixScore   = fuzzyScore("uvm_object", "uvm");
    auto scatteredScore = fuzzyScore("uvm_object", "uo");
    REQUIRE(prefixScore.has_value());
    REQUIRE(scatteredScore.has_value());
    CHECK(*prefixScore > *scatteredScore);
}

TEST_CASE("fuzzyScore: a contiguous run outranks an equal-length scattered match", "[fuzzy]")
{
    // Both patterns are length-3 subsequences of "uvm_report_object".
    auto contiguous = fuzzyScore("uvm_report_object", "rep");
    auto scattered   = fuzzyScore("uvm_report_object", "rpo");
    REQUIRE(contiguous.has_value());
    REQUIRE(scattered.has_value());
    CHECK(*contiguous > *scattered);
}

TEST_CASE("fuzzyScore: typo tolerant — a skipped character still matches", "[fuzzy]")
{
    // "wdth" skips the 'I' in "WIDTH" but is still a valid subsequence.
    auto s = fuzzyScore("WIDTH", "wdth");
    REQUIRE(s.has_value());
}

TEST_CASE("fuzzyScore: case-insensitive but exact case scores slightly higher", "[fuzzy]")
{
    auto exactCase  = fuzzyScore("uvm_object", "uvm");
    auto mixedCase  = fuzzyScore("uvm_object", "UVM");
    REQUIRE(exactCase.has_value());
    REQUIRE(mixedCase.has_value());
    CHECK(*exactCase > *mixedCase);
}

TEST_CASE("fuzzyScore: match at a word/camelCase boundary scores above mid-word", "[fuzzy]")
{
    // "p" matches the boundary right after '_' in uvm_phase far better than
    // the 'p' buried mid-word in "uvm_report".
    auto boundary = fuzzyScore("uvm_phase", "p");
    auto midWord  = fuzzyScore("uvm_report", "p");
    REQUIRE(boundary.has_value());
    REQUIRE(midWord.has_value());
    CHECK(*boundary > *midWord);
}

TEST_CASE("fuzzyScore: reordering the pattern's characters is not a match", "[fuzzy]")
{
    // Subsequence matching only allows skipping, never reordering.
    CHECK_FALSE(fuzzyScore("uvm_object", "obmvu").has_value());
}
