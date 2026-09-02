#include <catch2/catch_test_macros.hpp>
#include "lsp/sv_builtin_methods.h"
#include <algorithm>
#include <iterator>

namespace {
bool has(std::span<const BuiltinMethod> methods, std::string_view name)
{
    return std::any_of(methods.begin(), methods.end(),
                       [&](const BuiltinMethod& m){ return m.name == name; });
}
} // namespace

TEST_CASE("queue methods include push/pop but not associative-array-only members", "[sv-builtin-methods]")
{
    CHECK(has(QUEUE_METHODS, "push_back"));
    CHECK(has(QUEUE_METHODS, "pop_front"));
    CHECK(has(QUEUE_METHODS, "sort"));
    CHECK_FALSE(has(QUEUE_METHODS, "exists"));
    CHECK_FALSE(has(QUEUE_METHODS, "num"));
}

TEST_CASE("associative array methods have no locator/ordering methods", "[sv-builtin-methods]")
{
    CHECK(has(ASSOC_ARRAY_METHODS, "exists"));
    CHECK(has(ASSOC_ARRAY_METHODS, "num"));
    CHECK(has(ASSOC_ARRAY_METHODS, "sum"));
    CHECK_FALSE(has(ASSOC_ARRAY_METHODS, "sort"));
    CHECK_FALSE(has(ASSOC_ARRAY_METHODS, "push_back"));
}

TEST_CASE("fixed and dynamic arrays share locator methods but only dynamic has delete", "[sv-builtin-methods]")
{
    CHECK(has(FIXED_ARRAY_METHODS, "size"));
    CHECK(has(FIXED_ARRAY_METHODS, "sort"));
    CHECK_FALSE(has(FIXED_ARRAY_METHODS, "delete"));
    CHECK_FALSE(has(FIXED_ARRAY_METHODS, "push_back"));

    CHECK(has(DYNAMIC_ARRAY_METHODS, "delete"));
    CHECK(has(DYNAMIC_ARRAY_METHODS, "sort"));
    CHECK_FALSE(has(DYNAMIC_ARRAY_METHODS, "push_back"));
}

TEST_CASE("mailbox/semaphore/process tables hold their own distinct methods", "[sv-builtin-methods]")
{
    CHECK(has(MAILBOX_METHODS, "put"));
    CHECK(has(MAILBOX_METHODS, "get"));
    CHECK_FALSE(has(MAILBOX_METHODS, "randomize"));

    CHECK(has(SEMAPHORE_METHODS, "put"));
    CHECK(has(SEMAPHORE_METHODS, "try_get"));
    CHECK_FALSE(has(SEMAPHORE_METHODS, "get_randstate"));

    CHECK(has(PROCESS_METHODS, "status"));
    CHECK(has(PROCESS_METHODS, "self"));
    CHECK(has(PROCESS_METHODS, "get_randstate"));
}

TEST_CASE("none of the built-in tables offer a constructor", "[sv-builtin-methods]")
{
    CHECK_FALSE(has(QUEUE_METHODS, "new"));
    CHECK_FALSE(has(MAILBOX_METHODS, "new"));
    CHECK_FALSE(has(SEMAPHORE_METHODS, "new"));
    CHECK_FALSE(has(PROCESS_METHODS, "new"));
}

TEST_CASE("string methods table has representative entries", "[sv-builtin-methods]")
{
    CHECK(has(STRING_METHODS, "len"));
    CHECK(has(STRING_METHODS, "toupper"));
    CHECK(has(STRING_METHODS, "atoi"));
}

TEST_CASE("event has exactly the built-in 'triggered' property, not a method", "[sv-builtin-methods]")
{
    REQUIRE(std::size(EVENT_MEMBERS) == 1);
    CHECK(EVENT_MEMBERS[0].name == "triggered");
    CHECK(EVENT_MEMBERS[0].kind == lsp::CompletionItemKind::Property);
}

TEST_CASE("randomize-family table is separate from every container table", "[sv-builtin-methods]")
{
    CHECK(has(RANDOMIZE_METHODS, "randomize"));
    CHECK(has(RANDOMIZE_METHODS, "pre_randomize"));
    CHECK_FALSE(has(QUEUE_METHODS, "randomize"));
    CHECK_FALSE(has(ASSOC_ARRAY_METHODS, "randomize"));
}

TEST_CASE("builtinMethodsFor dispatches every tag/literal name and rejects an ordinary class", "[sv-builtin-methods]")
{
    CHECK(builtinMethodsFor(CONTAINER_QUEUE).size() == std::size(QUEUE_METHODS));
    CHECK(builtinMethodsFor(CONTAINER_ASSOC).size() == std::size(ASSOC_ARRAY_METHODS));
    CHECK(builtinMethodsFor(CONTAINER_DYNAMIC_ARRAY).size() == std::size(DYNAMIC_ARRAY_METHODS));
    CHECK(builtinMethodsFor(CONTAINER_FIXED_ARRAY).size() == std::size(FIXED_ARRAY_METHODS));
    CHECK(builtinMethodsFor(CONTAINER_STRING).size() == std::size(STRING_METHODS));
    CHECK(builtinMethodsFor(CONTAINER_EVENT).size() == std::size(EVENT_MEMBERS));
    CHECK(builtinMethodsFor("mailbox").size() == std::size(MAILBOX_METHODS));
    CHECK(builtinMethodsFor("semaphore").size() == std::size(SEMAPHORE_METHODS));
    CHECK(builtinMethodsFor("process").size() == std::size(PROCESS_METHODS));
    CHECK(builtinMethodsFor("MyClass").empty());
    CHECK(builtinMethodsFor("").empty());
}
