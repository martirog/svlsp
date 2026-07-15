#pragma once

#include <atomic>
#include <string>
#include <lsp/messages.h>
#include <lsp/error.h>

// ServerState encapsulates the LSP server lifecycle state machine with no I/O
// dependency, making it directly unit-testable.
//
// State transitions:
//   Uninitialized → (initialize) → Active
//   Active        → (shutdown)   → Shutdown
//   Shutdown      → (exit)       → Inactive
//
// Any request received in the wrong state throws lsp::RequestError.

class ServerState {
public:
    enum class Phase { Uninitialized, Active, Shutdown, Inactive };

    // Returns the initialize result; transitions Uninitialized → Active.
    auto handleInitialize(lsp::InitializeParams params)
        -> lsp::requests::Initialize::Result;

    // No-op: notification sent by the client after initialize completes.
    void handleInitialized();

    // Transitions Active → Shutdown.
    auto handleShutdown() -> lsp::requests::Shutdown::Result;

    // Transitions Shutdown → Inactive.
    void handleExit();

    Phase phase() const { return m_phase.load(); }

    // True while the server should keep processing messages.
    bool isRunning() const { return m_phase.load() != Phase::Inactive; }

    // The client's rootUri from `initialize`, if any. Captured for
    // completeness but NOT used to drive project discovery -- see
    // ProjectRegistry's header comment for why (upward search from each
    // opened file is used instead).
    const lsp::NullOr<lsp::DocumentUri>& rootUri() const { return m_rootUri; }

    // `initializationOptions.svlsp.projectConfig`, if present and a string;
    // "" if initializationOptions is absent, isn't an object, or the nested
    // path isn't there / isn't a string. An explicit path here always wins
    // over ProjectRegistry's upward-search discovery.
    const std::string& explicitProjectConfigPath() const { return m_explicitProjectConfigPath; }

private:
    std::atomic<Phase> m_phase{Phase::Uninitialized};
    lsp::NullOr<int>   m_parentProcessId;  // for parent-process exit monitoring
    lsp::NullOr<lsp::DocumentUri> m_rootUri;
    std::string        m_explicitProjectConfigPath;

    // Throws lsp::RequestError if the server is not in Active state.
    void requireActive(const char* method) const;

    static std::string extractProjectConfigPath(const lsp::InitializeParams& params);
};
