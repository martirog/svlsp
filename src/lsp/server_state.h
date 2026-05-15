#pragma once

#include <atomic>
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

private:
    std::atomic<Phase> m_phase{Phase::Uninitialized};
    lsp::NullOr<int>   m_parentProcessId;  // for parent-process exit monitoring

    // Throws lsp::RequestError if the server is not in Active state.
    void requireActive(const char* method) const;
};
