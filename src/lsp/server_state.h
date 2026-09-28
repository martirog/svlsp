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

    // `initializationOptions.svlsp.fuzzyCompletion`, if present and a
    // boolean; true (fuzzy matching stays on, today's unchanged behavior) if
    // initializationOptions is absent, isn't an object, or the nested value
    // isn't there / isn't a boolean. Resolved once here at `initialize` and
    // fixed for the server's lifetime -- see plan.md §6.11.
    bool fuzzyCompletionEnabled() const { return m_fuzzyCompletionEnabled; }

    // `initializationOptions.svlsp.docComments` (plan.md §6.31), same rules
    // as fuzzyCompletion: true unless present and false. Off, no doc
    // comments are collected into the DB (saves its space), so hover,
    // signature help and completion show none.
    bool docCommentsEnabled() const { return m_docCommentsEnabled; }

private:
    std::atomic<Phase> m_phase{Phase::Uninitialized};
    lsp::NullOr<int>   m_parentProcessId;  // for parent-process exit monitoring
    lsp::NullOr<lsp::DocumentUri> m_rootUri;
    std::string        m_explicitProjectConfigPath;
    bool               m_fuzzyCompletionEnabled{true};
    bool               m_docCommentsEnabled{true};

    // Throws lsp::RequestError if the server is not in Active state.
    void requireActive(const char* method) const;

    static std::string extractProjectConfigPath(const lsp::InitializeParams& params);
    // `initializationOptions.svlsp.<key>` when present and a boolean, else true.
    static bool extractBoolOption(const lsp::InitializeParams& params, std::string_view key);
};
