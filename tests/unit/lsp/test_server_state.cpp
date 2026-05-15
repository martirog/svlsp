#include <catch2/catch_test_macros.hpp>
#include "lsp/server_state.h"

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
