#pragma once

// The Unix-socket transport (plan.md §6.38): a server that outlives the
// editor (`svlsp --daemon`) and the editor-side relay that reaches it
// (`svlsp --socket`). The pieces here are the ones both sides share: the
// socket's path, the private directory it lives in, the listening and
// connecting ends, and an lsp::io::Stream over a connected descriptor.

#include <lsp/io/stream.h>
#include <chrono>
#include <optional>
#include <string>
#include <sys/types.h>

namespace unix_socket {

// sun_path's size, less the terminating NUL.
constexpr std::size_t kMaxPathLength = 107;

// The directory default sockets live in: /tmp/svlsp-<uid>. Not
// $XDG_RUNTIME_DIR, which is removed at the last logout and would orphan a
// server that outlives one (plan.md §6.38, D3).
std::string defaultDirectory();

// The default socket for a project root: <defaultDirectory()>/<hash>.sock,
// the hash of the root's absolute, normalized path (the scheme of the
// project DB's own default path, ProjectRegistry::defaultDatabasePath).
std::string defaultPath(const std::string& root);

// The root's absolute, normalized form without a trailing '/': what
// defaultPath hashes and what a server compares an editor's root with.
std::string normalizeRoot(const std::string& root);

// Creates `dir` mode 0700 if missing, then checks it: a directory (not a
// symlink), owned by this user, with no group or other access. "" when it
// is fit, else why not.
std::string ensurePrivateDirectory(const std::string& dir);

// "" when `path` fits sun_path, else the error to report.
std::string checkPathLength(const std::string& path);

// A connected socket as an lsp::io::Stream. Reads are buffered (the LSP
// connection reads headers a byte at a time); writes use MSG_NOSIGNAL, so
// a peer that went away is an lsp::io::Error, never a SIGPIPE. EOF or an
// error on read throws lsp::io::Error. Does not own the descriptor.
class FdStream : public lsp::io::Stream {
public:
    explicit FdStream(int fd) : m_fd{fd} {}

    void read(char* buffer, std::size_t size) override;
    void write(const char* buffer, std::size_t size) override;

private:
    int         m_fd;
    char        m_buf[65536];
    std::size_t m_pos{0};
    std::size_t m_len{0};
};

// The peer's credentials (SO_PEERCRED).
struct Peer {
    pid_t pid{0};
    uid_t uid{0};
};
std::optional<Peer> peerOf(int fd);

// Connects to the socket at `path`; -1 when nothing listens there.
int connectTo(const std::string& path);

// An exclusive advisory lock on <path>.lock, held while the object lives:
// serializes the start of a server for one socket (plan.md §6.38, the start
// race).
class StartLock {
public:
    explicit StartLock(const std::string& socketPath);
    ~StartLock();
    bool held() const { return m_fd >= 0; }
    StartLock(const StartLock&)            = delete;
    StartLock& operator=(const StartLock&) = delete;

private:
    int m_fd{-1};
};

// The listening end. bind() replaces a stale socket file (one nothing
// listens on), refuses a live one, and creates the file mode 0600
// whatever the umask.
class Listener {
public:
    Listener() = default;
    ~Listener();
    Listener(const Listener&)            = delete;
    Listener& operator=(const Listener&) = delete;

    // "" on success, else the error. Takes the StartLock itself.
    std::string bind(const std::string& path);

    // Waits up to `timeout` for a connection: its descriptor, or -1 on a
    // timeout (or an interrupted wait).
    int accept(std::chrono::milliseconds timeout);

    // Whether the file at the path is still the one bind() created (a /tmp
    // cleaner, or a user, may remove it: the server is then unreachable).
    bool stillOurs() const;

    // Closes the socket and removes its file, if it is still ours.
    void close();

    const std::string& path() const { return m_path; }
    int fd() const { return m_fd; }

private:
    int         m_fd{-1};
    std::string m_path;
    dev_t       m_dev{0};
    ino_t       m_ino{0};
};

// "30m", "24h", "2d", "45s", or a bare number of seconds; 0 = never.
// std::nullopt when malformed.
std::optional<std::chrono::seconds> parseDuration(const std::string& text);

} // namespace unix_socket
