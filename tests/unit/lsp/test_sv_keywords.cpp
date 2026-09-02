#include <catch2/catch_test_macros.hpp>
#include "lsp/sv_keywords.h"
#include <algorithm>

namespace {
bool contains(std::string_view name)
{
    return std::any_of(SV_KEYWORDS.begin(), SV_KEYWORDS.end(),
                       [&](const SvKeyword& k){ return k.text == name; });
}

unsigned contextsFor(std::string_view name)
{
    auto it = std::find_if(SV_KEYWORDS.begin(), SV_KEYWORDS.end(),
                           [&](const SvKeyword& k){ return k.text == name; });
    REQUIRE(it != SV_KEYWORDS.end());
    return it->contexts;
}
} // namespace

TEST_CASE("SV_KEYWORDS contains representative reserved words", "[sv-keywords]")
{
    CHECK(contains("module"));
    CHECK(contains("if"));
    CHECK(contains("endclass"));
    CHECK(contains("always_ff"));
    CHECK(contains("randomize"));
}

TEST_CASE("SV_KEYWORDS excludes non-keyword identifiers", "[sv-keywords]")
{
    CHECK_FALSE(contains("foo"));
    CHECK_FALSE(contains("myModule"));
    CHECK_FALSE(contains(""));
}

TEST_CASE("contextBitFor maps known scope kinds and defaults unknown ones to top level",
          "[sv-keywords]")
{
    CHECK(contextBitFor("") == KwTopLevel);
    CHECK(contextBitFor("Module") == KwModule);
    CHECK(contextBitFor("Interface") == KwInterface);
    CHECK(contextBitFor("Program") == KwProgram);
    CHECK(contextBitFor("Package") == KwPackage);
    CHECK(contextBitFor("Class") == KwClass);
    CHECK(contextBitFor("Function") == KwFunction);
    CHECK(contextBitFor("Task") == KwTask);
    CHECK(contextBitFor("SomethingUnknown") == KwTopLevel);
}

TEST_CASE("Design-unit declarations cannot nest -- top level only", "[sv-keywords]")
{
    // The user's own example: module/interface/program/package/primitive
    // declarations are illegal once already inside any other scope.
    CHECK(contextsFor("module") == KwTopLevel);
    CHECK(contextsFor("package") == KwTopLevel);
    CHECK((contextsFor("module") & KwModule) == 0);
    CHECK((contextsFor("module") & KwClass) == 0);
    // "endmodule" closes a module body while the cursor is still tracked as
    // inside it (scope kind Module), not at top level -- the opposite of
    // "module" itself.
    CHECK(contextsFor("endmodule") == KwModule);
    CHECK(contextsFor("endclass") == KwClass);
}

TEST_CASE("modport is interface-only", "[sv-keywords]")
{
    CHECK(contextsFor("modport") == KwInterface);
}

TEST_CASE("return is only legal inside a function or task body", "[sv-keywords]")
{
    unsigned ctx = contextsFor("return");
    CHECK((ctx & KwFunction) != 0);
    CHECK((ctx & KwTask) != 0);
    CHECK((ctx & KwTopLevel) == 0);
    CHECK((ctx & KwModule) == 0);
    CHECK((ctx & KwClass) == 0);
}
