#pragma once

// `svlsp --daemon` (plan.md §6.38): a server on a Unix socket that outlives
// the editor. One project, one editor at a time: each connection is a
// session (a fresh LanguageServer, built by the caller's Session); the
// parse worker and the project DB on disk carry over, so a reattach costs
// a refresh, not a load. A connection while a session runs gets an error
// reply to its `initialize` and is closed, unless the session ends within
// a grace period. With no editor attached for the
// idle timeout, the daemon exits; it also exits when its socket file is
// removed, and on SIGTERM / SIGINT / SIGHUP (when it handles signals).

#include "lsp/unix_socket.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class SocketDaemon {
public:
    struct Options {
        std::string socketPath;
        std::string root; // the project root it serves (normalized), for messages
        std::chrono::seconds idleTimeout{std::chrono::hours{24}}; // 0 = never
        std::chrono::milliseconds pollInterval{1000};
        std::chrono::seconds orphanCheckInterval{60};
        // A connection while a session runs waits this long for it to end
        // (an editor restarting reconnects while its old session is still
        // being torn down) before it is refused.
        std::chrono::milliseconds busyGrace{5000};
        // Run once at start, as a child process (stdin /dev/null, stdout to
        // stderr): the project's `--build-db`. Empty = none.
        std::vector<std::string> preload;
        // Waits for SIGTERM / SIGINT / SIGHUP on a thread of its own; the
        // caller must have blocked them (in every thread) before any thread
        // was started: blockStopSignals().
        bool handleSignals{false};
        std::ostream* log{nullptr}; // "[daemon] ..." lines; null = none
    };

    // One editor's session over `io`, from its connection to its end (the
    // editor's `exit`, or the connection lost).
    using Session = std::function<void(lsp::io::Stream& io, const unix_socket::Peer& peer)>;

    SocketDaemon(Options options, Session session);
    ~SocketDaemon();

    // Binds the socket (an error -> exit code 1, logged), then serves until
    // stopped. Returns the exit code.
    int run();

    // Ends the daemon from any thread: the session, if one runs, loses its
    // connection; run() returns.
    void stop(const std::string& why);

    // Blocks SIGTERM, SIGINT and SIGHUP in the calling thread (and every
    // thread it starts afterwards), for handleSignals.
    static void blockStopSignals();

    // The error reply a connection gets while another editor is attached:
    // reads its first message (up to `timeout`) and answers it, if it is a
    // request, with `message`. Then the caller closes it.
    static void refuse(int fd, const std::string& message, std::chrono::milliseconds timeout);

private:
    void acceptLoop();
    void signalLoop();
    void startPreload();
    void reapPreload(bool wait);
    void logLine(const std::string& line);

    Options   m_options;
    Session   m_session;
    unix_socket::Listener m_listener;

    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool>       m_stop{false};
    std::string             m_stopWhy;
    int                     m_pending{-1};    // an accepted connection for run()
    int                     m_sessionFd{-1};  // the attached editor's, -1 = none
    std::chrono::steady_clock::time_point m_idleSince;
    std::vector<std::thread> m_waiters; // connections waiting for the session to end
    void waitOrRefuse(int fd, pid_t pid);

    pid_t m_preloadPid{0};
    std::mutex m_logMutex;
};
