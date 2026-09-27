#include <catch2/catch_test_macros.hpp>
#include "lsp/server_state.h"
#include <lsp/json/json.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lsp::InitializeParams makeParams(std::optional<int> pid = {})
{
    lsp::InitializeParams p;
    p.processId = pid ? lsp::NullOr<int>(*pid) : lsp::NullOr<int>{};
    return p;
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: initial phase is Uninitialized", "[lifecycle]")
{
    ServerState s;
    REQUIRE(s.phase() == ServerState::Phase::Uninitialized);
    REQUIRE(s.isRunning());
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: initialize transitions to Active", "[lifecycle]")
{
    ServerState s;
    auto result = s.handleInitialize(makeParams());
    REQUIRE(s.phase() == ServerState::Phase::Active);
    REQUIRE(s.isRunning());
}

TEST_CASE("ServerState: initialize returns correct server name", "[lifecycle]")
{
    ServerState s;
    auto result = s.handleInitialize(makeParams());
    REQUIRE(result.serverInfo.has_value());
    REQUIRE(result.serverInfo->name == "svlsp");
}

TEST_CASE("ServerState: initialize returns textDocumentSync capability", "[lifecycle]")
{
    ServerState s;
    auto result = s.handleInitialize(makeParams());
    REQUIRE(result.capabilities.textDocumentSync.has_value());
}

TEST_CASE("ServerState: signature help triggers on '(' and ',' and re-triggers on ';'",
          "[lifecycle][signature_help]")
{
    ServerState s;
    auto result = s.handleInitialize(makeParams());
    REQUIRE(result.capabilities.signatureHelpProvider.has_value());
    const auto& opts = *result.capabilities.signatureHelpProvider;
    REQUIRE(opts.triggerCharacters.has_value());
    CHECK(*opts.triggerCharacters == lsp::Array<lsp::String>{"(", ","});
    // `for (init; cond; step)` moves to the next slot on ';'.
    REQUIRE(opts.retriggerCharacters.has_value());
    CHECK(*opts.retriggerCharacters == lsp::Array<lsp::String>{";"});
}

TEST_CASE("ServerState: double initialize throws RequestError", "[lifecycle]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    REQUIRE_THROWS_AS(s.handleInitialize(makeParams()), lsp::RequestError);
}

// ---------------------------------------------------------------------------
// initialized notification
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: handleInitialized is a no-op", "[lifecycle]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    REQUIRE_NOTHROW(s.handleInitialized());
    REQUIRE(s.phase() == ServerState::Phase::Active);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: shutdown transitions Active to Shutdown", "[lifecycle]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    s.handleShutdown();
    REQUIRE(s.phase() == ServerState::Phase::Shutdown);
    REQUIRE(s.isRunning());  // still running until exit
}

TEST_CASE("ServerState: shutdown before initialize throws RequestError", "[lifecycle]")
{
    ServerState s;
    REQUIRE_THROWS_AS(s.handleShutdown(), lsp::RequestError);
}

TEST_CASE("ServerState: double shutdown throws RequestError", "[lifecycle]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    s.handleShutdown();
    REQUIRE_THROWS_AS(s.handleShutdown(), lsp::RequestError);
}

// ---------------------------------------------------------------------------
// exit
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: exit after shutdown transitions to Inactive", "[lifecycle]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    s.handleShutdown();
    s.handleExit();
    REQUIRE(s.phase() == ServerState::Phase::Inactive);
    REQUIRE_FALSE(s.isRunning());
}

TEST_CASE("ServerState: exit without shutdown also transitions to Inactive", "[lifecycle]")
{
    // LSP spec allows exit without prior shutdown (abnormal termination).
    ServerState s;
    s.handleInitialize(makeParams());
    s.handleExit();
    REQUIRE(s.phase() == ServerState::Phase::Inactive);
    REQUIRE_FALSE(s.isRunning());
}

// ---------------------------------------------------------------------------
// rootUri / explicitProjectConfigPath (Phase 6.2 Stage 5)
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: rootUri is captured from initialize params",
          "[lsp][server-state][project]")
{
    ServerState s;
    auto params = makeParams();
    params.rootUri = lsp::DocumentUri::fromPath("/some/workspace");
    s.handleInitialize(params);

    REQUIRE_FALSE(s.rootUri().isNull());
    CHECK(s.rootUri().value().path() == "/some/workspace");
}

TEST_CASE("ServerState: explicitProjectConfigPath is empty with no initializationOptions",
          "[lsp][server-state][project]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    CHECK(s.explicitProjectConfigPath().empty());
}

TEST_CASE("ServerState: explicitProjectConfigPath is empty when initializationOptions "
          "isn't an object", "[lsp][server-state][project]")
{
    ServerState s;
    auto params = makeParams();
    params.initializationOptions = lsp::json::Value(lsp::json::String("not an object"));
    REQUIRE_NOTHROW(s.handleInitialize(params));
    CHECK(s.explicitProjectConfigPath().empty());
}

TEST_CASE("ServerState: explicitProjectConfigPath is empty when \"svlsp\" key is missing",
          "[lsp][server-state][project]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object root;
    root["somethingElse"] = lsp::json::Value(lsp::json::String("x"));
    params.initializationOptions = lsp::json::Value(std::move(root));
    REQUIRE_NOTHROW(s.handleInitialize(params));
    CHECK(s.explicitProjectConfigPath().empty());
}

TEST_CASE("ServerState: explicitProjectConfigPath is empty when projectConfig isn't a string",
          "[lsp][server-state][project]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object svlsp;
    svlsp["projectConfig"] = lsp::json::Value(lsp::json::Integer(5));
    lsp::json::Object root;
    root["svlsp"] = lsp::json::Value(std::move(svlsp));
    params.initializationOptions = lsp::json::Value(std::move(root));
    REQUIRE_NOTHROW(s.handleInitialize(params));
    CHECK(s.explicitProjectConfigPath().empty());
}

TEST_CASE("ServerState: explicitProjectConfigPath extracts a nested string path",
          "[lsp][server-state][project]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object svlsp;
    svlsp["projectConfig"] = lsp::json::Value(lsp::json::String("/proj/.svlsp.json"));
    lsp::json::Object root;
    root["svlsp"] = lsp::json::Value(std::move(svlsp));
    params.initializationOptions = lsp::json::Value(std::move(root));
    s.handleInitialize(params);
    CHECK(s.explicitProjectConfigPath() == "/proj/.svlsp.json");
}

// ---------------------------------------------------------------------------
// fuzzyCompletionEnabled (plan.md §6.11)
// ---------------------------------------------------------------------------

TEST_CASE("ServerState: fuzzyCompletionEnabled defaults to true with no initializationOptions",
          "[lsp][server-state][completion]")
{
    ServerState s;
    s.handleInitialize(makeParams());
    CHECK(s.fuzzyCompletionEnabled());
}

TEST_CASE("ServerState: fuzzyCompletionEnabled defaults to true when initializationOptions "
          "isn't an object", "[lsp][server-state][completion]")
{
    ServerState s;
    auto params = makeParams();
    params.initializationOptions = lsp::json::Value(lsp::json::String("not an object"));
    REQUIRE_NOTHROW(s.handleInitialize(params));
    CHECK(s.fuzzyCompletionEnabled());
}

TEST_CASE("ServerState: fuzzyCompletionEnabled defaults to true when \"svlsp\" key is missing",
          "[lsp][server-state][completion]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object root;
    root["somethingElse"] = lsp::json::Value(lsp::json::String("x"));
    params.initializationOptions = lsp::json::Value(std::move(root));
    REQUIRE_NOTHROW(s.handleInitialize(params));
    CHECK(s.fuzzyCompletionEnabled());
}

TEST_CASE("ServerState: fuzzyCompletionEnabled defaults to true when fuzzyCompletion isn't "
          "a boolean", "[lsp][server-state][completion]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object svlsp;
    svlsp["fuzzyCompletion"] = lsp::json::Value(lsp::json::String("nope"));
    lsp::json::Object root;
    root["svlsp"] = lsp::json::Value(std::move(svlsp));
    params.initializationOptions = lsp::json::Value(std::move(root));
    REQUIRE_NOTHROW(s.handleInitialize(params));
    CHECK(s.fuzzyCompletionEnabled());
}

TEST_CASE("ServerState: fuzzyCompletionEnabled is true when explicitly set true",
          "[lsp][server-state][completion]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object svlsp;
    svlsp["fuzzyCompletion"] = lsp::json::Value(lsp::json::Boolean(true));
    lsp::json::Object root;
    root["svlsp"] = lsp::json::Value(std::move(svlsp));
    params.initializationOptions = lsp::json::Value(std::move(root));
    s.handleInitialize(params);
    CHECK(s.fuzzyCompletionEnabled());
}

TEST_CASE("ServerState: fuzzyCompletionEnabled is false when explicitly set false",
          "[lsp][server-state][completion]")
{
    ServerState s;
    auto params = makeParams();
    lsp::json::Object svlsp;
    svlsp["fuzzyCompletion"] = lsp::json::Value(lsp::json::Boolean(false));
    lsp::json::Object root;
    root["svlsp"] = lsp::json::Value(std::move(svlsp));
    params.initializationOptions = lsp::json::Value(std::move(root));
    s.handleInitialize(params);
    CHECK_FALSE(s.fuzzyCompletionEnabled());
}
