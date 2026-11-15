// The Unix-socket transport (plan.md §6.38): the socket's path and private
// directory, the listener (mode, stale and live files), the stream, the
// daemon's sessions (one after another, a second editor refused, idle exit,
// orphan exit, stop), the relay, and the server's required root.

#include <catch2/catch_test_macros.hpp>
#include "lsp/server.h"
#include "lsp/socket_daemon.h"
#include "lsp/socket_relay.h"
#include "lsp/unix_socket.h"
#include <lsp/connection.h>
#include <lsp/json/json.h>
#include <lsp/jsonrpc/jsonrpc.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using namespace unix_socket;

namespace {

// A short, private scratch directory (sun_path is 108 bytes).
struct TempDir {
    std::string path;
    TempDir()
    {
        path = "/tmp/svlsp_ut_" + std::to_string(::getpid()) + "_" + std::to_string(counter()++);
        fs::remove_all(path);
        fs::create_directories(path);
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
    }
    ~TempDir() { fs::remove_all(path); }
    static int& counter()
    {
        static int n = 0;
        return n;
    }
};

std::string frame(const std::string& body)
{
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string initializeJson(int id, const std::string& root)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
           ",\"method\":\"initialize\",\"params\":{\"processId\":null,\"rootUri\":" +
           (root.empty() ? std::string{"null"} : "\"file://" + root + "\"") +
           ",\"capabilities\":{}}}";
}

// The first response on `conn`: its result or error, as JSON text.
lsp::jsonrpc::Response readResponse(lsp::Connection& conn)
{
    while (true) {
        auto msg = conn.readMessage();
        auto* single = std::get_if<lsp::jsonrpc::Message>(&msg);
        if (!single) continue;
        if (auto* r = std::get_if<lsp::jsonrpc::Response>(single)) return std::move(*r);
    }
}

void sendRaw(int fd, const std::string& bytes)
{
    REQUIRE(::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) ==
            static_cast<ssize_t>(bytes.size()));
}

// A daemon on its own thread, with fast timers.
struct RunningDaemon {
    SocketDaemon daemon;
    std::thread  thread;
    std::atomic<int> rc{-1};

    RunningDaemon(SocketDaemon::Options o, SocketDaemon::Session s)
        : daemon{std::move(o), std::move(s)}
    {
        thread = std::thread{[this] { rc = daemon.run(); }};
    }
    ~RunningDaemon()
    {
        daemon.stop("test over");
        if (thread.joinable()) thread.join();
    }
    bool waitStopped(std::chrono::milliseconds limit)
    {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (rc == -1 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(10ms);
        return rc != -1;
    }
};

SocketDaemon::Options fastOptions(const std::string& path)
{
    SocketDaemon::Options o;
    o.socketPath          = path;
    o.root                = "/proj";
    o.pollInterval        = 20ms;
    o.orphanCheckInterval = 0s;
    o.idleTimeout         = 0s;
    o.busyGrace           = 300ms;
    return o;
}

// Waits for the daemon to listen, without connecting (a connection would
// be a session, and keep the next one waiting).
void waitListening(const std::string& path)
{
    for (int i = 0; i < 200 && !fs::exists(path); ++i) std::this_thread::sleep_for(10ms);
    std::this_thread::sleep_for(30ms); // bind -> listen
    REQUIRE(fs::exists(path));
}

int connectWithRetry(const std::string& path)
{
    for (int i = 0; i < 200; ++i) {
        if (const int fd = connectTo(path); fd >= 0) return fd;
        std::this_thread::sleep_for(10ms);
    }
    return -1;
}

// A session that answers every request with its method name, and ends on
// `exit` (or the connection lost).
void echoSession(lsp::io::Stream& io, const Peer&)
{
    lsp::Connection conn{io};
    try {
        while (true) {
            auto msg = conn.readMessage();
            auto* single = std::get_if<lsp::jsonrpc::Message>(&msg);
            if (!single) continue;
            auto* req = std::get_if<lsp::jsonrpc::Request>(single);
            if (!req) continue;
            if (req->method == "exit") return;
            if (req->id)
                conn.writeMessage(lsp::jsonrpc::Message{
                    lsp::jsonrpc::createResponse(*req->id, lsp::json::String{req->method})});
        }
    } catch (const std::exception&) {
    }
}

} // namespace

TEST_CASE("unix socket: durations", "[unixsocket][phase6.38]")
{
    CHECK(parseDuration("24h") == std::chrono::seconds{86400});
    CHECK(parseDuration("30m") == std::chrono::seconds{1800});
    CHECK(parseDuration("2d") == std::chrono::seconds{172800});
    CHECK(parseDuration("45s") == std::chrono::seconds{45});
    CHECK(parseDuration("90") == std::chrono::seconds{90});
    CHECK(parseDuration("0") == std::chrono::seconds{0});
    CHECK_FALSE(parseDuration(""));
    CHECK_FALSE(parseDuration("h"));
    CHECK_FALSE(parseDuration("5x"));
    CHECK_FALSE(parseDuration("5hm"));
    CHECK_FALSE(parseDuration("-5"));
}

TEST_CASE("unix socket: the default path is per root, in /tmp/svlsp-<uid>", "[unixsocket][phase6.38]")
{
    TempDir a, b;
    const std::string dir = "/tmp/svlsp-" + std::to_string(::getuid());
    CHECK(defaultDirectory() == dir);
    const std::string p = defaultPath(a.path);
    CHECK(p.rfind(dir + "/", 0) == 0);
    CHECK(p.ends_with(".sock"));
    CHECK(p.size() == dir.size() + 1 + 16 + 5);
    CHECK(defaultPath(a.path + "/") == p);
    CHECK(defaultPath(a.path + "/sub/..") == p);
    CHECK(defaultPath(b.path) != p);
    CHECK(normalizeRoot(a.path + "/") == a.path);
}

TEST_CASE("unix socket: the private directory", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string dir = t.path + "/d";
    CHECK(ensurePrivateDirectory(dir) == "");
    struct stat st{};
    REQUIRE(::stat(dir.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);
    CHECK(ensurePrivateDirectory(dir) == ""); // exists: fine

    ::chmod(dir.c_str(), 0755);
    CHECK(ensurePrivateDirectory(dir).find("open to other users (mode 755") != std::string::npos);

    const std::string link = t.path + "/l";
    fs::create_directory_symlink(dir, link);
    CHECK(ensurePrivateDirectory(link).find("not a directory") != std::string::npos);

    std::ofstream(t.path + "/f") << "x";
    CHECK(ensurePrivateDirectory(t.path + "/f").find("not a directory") != std::string::npos);
}

TEST_CASE("unix socket: the listener's file is 0600, whatever the umask", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/s.sock";
    const mode_t old = ::umask(0);
    Listener l;
    const std::string e = l.bind(path);
    ::umask(old);
    REQUIRE(e == "");
    struct stat st{};
    REQUIRE(::lstat(path.c_str(), &st) == 0);
    CHECK(S_ISSOCK(st.st_mode));
    CHECK((st.st_mode & 0777) == 0600);
    CHECK(l.stillOurs());
    l.close();
    CHECK_FALSE(fs::exists(path));
}

TEST_CASE("unix socket: a stale file is replaced, a live one refused", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/s.sock";
    {
        // A server that died: its file stays, nothing listens.
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.c_str(), path.size());
        REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
        ::close(fd);
    }
    REQUIRE(fs::exists(path));
    Listener l;
    CHECK(l.bind(path) == "");

    Listener second;
    CHECK(second.bind(path).find("another svlsp already listens") != std::string::npos);
    CHECK(l.stillOurs()); // the refused one left the live file alone

    std::ofstream(t.path + "/plain") << "x";
    Listener third;
    CHECK(third.bind(t.path + "/plain").find("is not a socket") != std::string::npos);
}

TEST_CASE("unix socket: too long a path is an error", "[unixsocket][phase6.38]")
{
    const std::string path = "/tmp/" + std::string(120, 'x') + ".sock";
    CHECK(checkPathLength(path).find("at most 107") != std::string::npos);
    Listener l;
    CHECK(l.bind(path).find("at most 107") != std::string::npos);
    CHECK(connectTo(path) == -1);
}

TEST_CASE("unix socket: a removed socket file is noticed", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/s.sock";
    Listener l;
    REQUIRE(l.bind(path) == "");
    fs::remove(path);
    CHECK_FALSE(l.stillOurs());
    // Another file at the path isn't ours either.
    Listener other;
    REQUIRE(other.bind(path) == "");
    CHECK_FALSE(l.stillOurs());
    l.close();
    CHECK(fs::exists(path)); // close() left the other's file
}

TEST_CASE("unix socket: the stream frames LSP messages; a gone peer is an error, not a signal",
          "[unixsocket][phase6.38]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    FdStream server{fds[0]};
    lsp::Connection conn{server};

    // Two messages, the first split mid-header, the second in the same write.
    const std::string a = frame(R"({"jsonrpc":"2.0","id":1,"method":"one"})");
    const std::string b = frame(R"({"jsonrpc":"2.0","id":2,"method":"two"})");
    sendRaw(fds[1], a.substr(0, 7));
    std::thread later{[&] {
        std::this_thread::sleep_for(20ms);
        sendRaw(fds[1], a.substr(7) + b);
    }};
    for (const char* method : {"one", "two"}) {
        auto msg = conn.readMessage();
        auto& req = std::get<lsp::jsonrpc::Request>(std::get<lsp::jsonrpc::Message>(msg));
        CHECK(req.method == method);
    }
    later.join();

    const auto peer = peerOf(fds[0]);
    REQUIRE(peer);
    CHECK(peer->uid == ::getuid());
    CHECK(peer->pid == ::getpid());

    ::close(fds[1]);
    char c;
    CHECK_THROWS_AS(server.read(&c, 1), lsp::io::Error);
    // Without MSG_NOSIGNAL this would SIGPIPE the test binary.
    const std::string big(1 << 20, 'x');
    CHECK_THROWS_AS(server.write(big.data(), big.size()), lsp::io::Error);
    ::close(fds[0]);
}

TEST_CASE("unix socket: the root an initialize names", "[unixsocket][phase6.38]")
{
    CHECK(rootFromInitialize(initializeJson(1, "/a/b")) == "/a/b");
    CHECK(rootFromInitialize(R"({"params":{"rootUri":"file:///a/b%20c"}})") == "/a/b c");
    CHECK(rootFromInitialize(R"({"params":{"rootUri":null,"rootPath":"/r/p"}})") == "/r/p");
    CHECK(rootFromInitialize(
              R"({"params":{"rootUri":null,"workspaceFolders":[{"uri":"file:///w/f","name":"f"}]}})") ==
          "/w/f");
    CHECK(rootFromInitialize(R"({"params":{"rootUri":null}})") == "");
    CHECK(rootFromInitialize("not json") == "");
    CHECK(logPathFor("/t/x.sock") == "/t/x.log");
    CHECK(logPathFor("/t/x") == "/t/x.log");
}

TEST_CASE("unix socket daemon: editors attach one after another", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    std::atomic<int> sessions{0};
    RunningDaemon d{fastOptions(path), [&](lsp::io::Stream& io, const Peer& p) {
                        ++sessions;
                        CHECK(p.uid == ::getuid());
                        echoSession(io, p);
                    }};
    for (int i = 1; i <= 3; ++i) {
        const int fd = connectWithRetry(path);
        REQUIRE(fd >= 0);
        FdStream s{fd};
        lsp::Connection conn{s};
        sendRaw(fd, frame(initializeJson(i, "/proj")));
        const auto r = readResponse(conn);
        CHECK(lsp::json::stringify(r.result.value()) == "\"initialize\"");
        if (i == 2) {
            ::close(fd); // dropped, no exit: the session ends all the same
            continue;
        }
        sendRaw(fd, frame(R"({"jsonrpc":"2.0","method":"exit"})"));
        char c;
        CHECK(::recv(fd, &c, 1, 0) == 0); // the daemon closed it
        ::close(fd);
    }
    // The last session's end is counted once its thread got back.
    for (int i = 0; i < 100 && sessions < 3; ++i) std::this_thread::sleep_for(10ms);
    CHECK(sessions == 3);
    CHECK(fs::exists(path));
}

TEST_CASE("unix socket daemon: a second editor gets an error reply", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    RunningDaemon d{fastOptions(path), echoSession};
    const int first = connectWithRetry(path);
    REQUIRE(first >= 0);
    FdStream s1{first};
    lsp::Connection c1{s1};
    sendRaw(first, frame(initializeJson(1, "/proj")));
    readResponse(c1); // attached

    const int second = connectTo(path);
    REQUIRE(second >= 0);
    FdStream s2{second};
    lsp::Connection c2{s2};
    sendRaw(second, frame(initializeJson(7, "/proj")));
    const auto r = readResponse(c2);
    REQUIRE(r.error);
    CHECK(r.error->message == "svlsp: the server for /proj is in use by another editor");
    CHECK(std::get<lsp::json::Integer>(r.id) == 7);
    char c;
    CHECK(::recv(second, &c, 1, 0) == 0);
    ::close(second);

    // The first is still served.
    sendRaw(first, frame(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"));
    CHECK(lsp::json::stringify(readResponse(c1).result.value()) == "\"ping\"");
    ::close(first);
}

TEST_CASE("unix socket daemon: an editor reconnecting while its old session ends is served",
          "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    // A session whose teardown takes a while after its connection is gone.
    RunningDaemon d{fastOptions(path), [](lsp::io::Stream& io, const Peer& p) {
                        echoSession(io, p);
                        std::this_thread::sleep_for(150ms);
                    }};
    const int first = connectWithRetry(path);
    REQUIRE(first >= 0);
    sendRaw(first, frame(R"({"jsonrpc":"2.0","method":"exit"})"));
    ::close(first);
    const int second = connectTo(path);
    REQUIRE(second >= 0);
    FdStream s{second};
    lsp::Connection conn{s};
    sendRaw(second, frame(initializeJson(1, "/proj")));
    const auto r = readResponse(conn);
    CHECK_FALSE(r.error);
    ::close(second);
}

TEST_CASE("unix socket daemon: idle timeout, from start and from the last detach",
          "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    {
        auto o        = fastOptions(path);
        o.idleTimeout = 1s;
        RunningDaemon d{o, echoSession};
        CHECK(d.waitStopped(3s)); // nobody ever attached
        CHECK(d.rc == 0);
        CHECK_FALSE(fs::exists(path));
    }
    {
        auto o        = fastOptions(path);
        o.idleTimeout = 1s;
        RunningDaemon d{o, echoSession};
        const int fd = connectWithRetry(path);
        REQUIRE(fd >= 0);
        std::this_thread::sleep_for(1500ms); // attached: no idle exit
        CHECK(d.rc == -1);
        ::close(fd);
        CHECK(d.waitStopped(3s));
        CHECK_FALSE(fs::exists(path));
    }
}

TEST_CASE("unix socket daemon: exits when its socket file is removed", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    RunningDaemon d{fastOptions(path), echoSession};
    waitListening(path);
    fs::remove(path);
    CHECK(d.waitStopped(3s));
}

TEST_CASE("unix socket daemon: stop() ends an attached session", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    RunningDaemon d{fastOptions(path), echoSession};
    const int fd = connectWithRetry(path);
    REQUIRE(fd >= 0);
    FdStream s{fd};
    lsp::Connection conn{s};
    sendRaw(fd, frame(initializeJson(1, "/proj")));
    readResponse(conn);
    d.daemon.stop("test");
    CHECK(d.waitStopped(3s));
    char c;
    CHECK(::recv(fd, &c, 1, 0) == 0);
    ::close(fd);
    CHECK_FALSE(fs::exists(path));
}

TEST_CASE("unix socket daemon: a second daemon on the socket refuses to start",
          "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    RunningDaemon d{fastOptions(path), echoSession};
    waitListening(path);
    SocketDaemon second{fastOptions(path), echoSession};
    CHECK(second.run() == 1);
    CHECK(fs::exists(path));
}

TEST_CASE("unix socket: the relay carries a session both ways", "[unixsocket][phase6.38]")
{
    TempDir t;
    const std::string path = t.path + "/d.sock";
    RunningDaemon d{fastOptions(path), echoSession};
    waitListening(path);

    int toRelay[2], fromRelay[2];
    REQUIRE(::pipe(toRelay) == 0);
    REQUIRE(::pipe(fromRelay) == 0);
    std::atomic<int> rc{-1};
    std::thread relay{[&] {
        RelayOptions o;
        o.root       = "/proj";
        o.socketPath = path;
        rc = runRelay(o, toRelay[0], fromRelay[1]);
        ::close(fromRelay[1]);
    }};
    // The initialize and a request in one write: the relay reads the first
    // and must forward the rest too.
    const std::string bytes = frame(initializeJson(1, "/proj")) +
                              frame(R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
    REQUIRE(::write(toRelay[1], bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));

    struct PipeIn : lsp::io::Stream {
        int fd;
        explicit PipeIn(int f) : fd{f} {}
        void read(char* b, std::size_t n) override
        {
            while (n > 0) {
                const ssize_t r = ::read(fd, b, n);
                if (r <= 0) throw lsp::io::Error{"eof"};
                b += r;
                n -= static_cast<std::size_t>(r);
            }
        }
        void write(const char*, std::size_t) override {}
    } in{fromRelay[0]};
    lsp::Connection conn{in};
    CHECK(lsp::json::stringify(readResponse(conn).result.value()) == "\"initialize\"");
    CHECK(lsp::json::stringify(readResponse(conn).result.value()) == "\"ping\"");

    // The editor closes stdin: the session ends, and so does the relay.
    ::close(toRelay[1]);
    relay.join();
    CHECK(rc == 0);
    ::close(toRelay[0]);
    ::close(fromRelay[0]);
}

TEST_CASE("unix socket: the server refuses an initialize for another root",
          "[unixsocket][phase6.38]")
{
    TempDir t, other;
    const std::string path = t.path + "/d.sock";
    const std::string root = normalizeRoot(t.path);
    RunningDaemon d{fastOptions(path), [&](lsp::io::Stream& io, const Peer&) {
                        LanguageServer server{io};
                        server.setRequiredRoot(root);
                        server.run();
                    }};
    {
        const int fd = connectWithRetry(path);
        REQUIRE(fd >= 0);
        FdStream s{fd};
        lsp::Connection conn{s};
        sendRaw(fd, frame(initializeJson(1, other.path)));
        const auto r = readResponse(conn);
        REQUIRE(r.error);
        CHECK(r.error->message == "svlsp: this server serves " + root + ", not " +
                                      normalizeRoot(other.path) +
                                      " (start `svlsp --socket` from that project)");
        ::close(fd);
    }
    {
        const int fd = connectWithRetry(path);
        REQUIRE(fd >= 0);
        FdStream s{fd};
        lsp::Connection conn{s};
        sendRaw(fd, frame(initializeJson(1, root + "/")));
        CHECK_FALSE(readResponse(conn).error);
        ::close(fd);
    }
    {
        // No root at all: served.
        const int fd = connectWithRetry(path);
        REQUIRE(fd >= 0);
        FdStream s{fd};
        lsp::Connection conn{s};
        sendRaw(fd, frame(initializeJson(1, "")));
        CHECK_FALSE(readResponse(conn).error);
        ::close(fd);
    }
}
