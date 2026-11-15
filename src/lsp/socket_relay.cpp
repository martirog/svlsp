#include "lsp/socket_relay.h"
#include "lsp/unix_socket.h"
#include <lsp/json/json.h>
#include <lsp/uri.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

bool writeAll(int fd, const char* data, std::size_t size)
{
    while (size > 0) {
        const ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0 && errno == ENOTSOCK) {
            const ssize_t w = ::write(fd, data, size);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) return false;
            data += w;
            size -= static_cast<std::size_t>(w);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

// Reads the first LSP message off `fd` into `raw` (header and body, to be
// forwarded as they came) and its body into `body`. False on EOF / error.
bool readFirstMessage(int fd, std::string& raw, std::string& body)
{
    char buf[4096];
    std::size_t headerEnd = std::string::npos;
    std::size_t length    = 0;
    while (true) {
        if (headerEnd == std::string::npos) {
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                std::string header = raw.substr(0, headerEnd);
                for (auto& c : header) c = static_cast<char>(std::tolower(c));
                const auto at = header.find("content-length:");
                if (at == std::string::npos) return false;
                length = std::strtoul(header.c_str() + at + 15, nullptr, 10);
                headerEnd += 4;
            }
        }
        if (headerEnd != std::string::npos && raw.size() >= headerEnd + length) {
            body = raw.substr(headerEnd, length);
            return true;
        }
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        raw.append(buf, static_cast<std::size_t>(n));
    }
}

// Starts `argv` detached: in a session of its own (no terminal, no
// signals from the editor's process group), stdin and stdout on /dev/null,
// stderr appended to `logPath`. Returns once it has been started.
bool startDetached(const std::vector<std::string>& args, const std::string& logPath)
{
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child < 0) return false;
    if (child == 0) {
        ::setsid();
        if (::fork() != 0) _exit(0); // the server is reparented to init
        const int null = ::open("/dev/null", O_RDWR);
        const int log  = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        ::dup2(null, 0);
        ::dup2(null, 1);
        ::dup2(log >= 0 ? log : null, 2);
        for (int fd = 3; fd < 1024; ++fd) ::close(fd);
        if (::chdir("/") != 0) {}
        sigset_t none;
        sigemptyset(&none);
        pthread_sigmask(SIG_SETMASK, &none, nullptr);
        ::execv(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return true;
}

// Copies in -> sock and sock -> out until the server closes the socket.
// The editor closing `in` half-closes the socket, so the server ends the
// session and closes it.
int relay(int in, int out, int sock)
{
    char buf[65536];
    bool inOpen = true;
    while (true) {
        pollfd fds[2] = {{.fd = sock, .events = POLLIN, .revents = 0},
                         {.fd = inOpen ? in : -1, .events = POLLIN, .revents = 0}};
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        if (fds[0].revents) {
            const ssize_t n = ::recv(sock, buf, sizeof buf, 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return 0; // the server ended the session
            if (!writeAll(out, buf, static_cast<std::size_t>(n))) return 0; // the editor is gone
        }
        if (inOpen && fds[1].revents) {
            const ssize_t n = ::read(in, buf, sizeof buf);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                inOpen = false;
                ::shutdown(sock, SHUT_WR);
                continue;
            }
            if (!writeAll(sock, buf, static_cast<std::size_t>(n))) return 0;
        }
    }
}

} // namespace

std::string rootFromInitialize(const std::string& json)
{
    try {
        const auto v = lsp::json::parse(json);
        if (!v.isObject()) return "";
        const auto* params = v.object().find("params");
        if (!params || !params->isObject()) return "";
        const auto& p = params->object();
        auto fromUri = [](const std::string& uri) {
            return std::string{lsp::Uri::parse(uri).path()};
        };
        if (const auto* u = p.find("rootUri"); u && u->isString() && !u->string().empty())
            return fromUri(u->string());
        if (const auto* r = p.find("rootPath"); r && r->isString() && !r->string().empty())
            return r->string();
        if (const auto* w = p.find("workspaceFolders"); w && w->isArray() && !w->array().empty()) {
            const auto& f = w->array().front();
            if (f.isObject())
                if (const auto* u = f.object().find("uri"); u && u->isString())
                    return fromUri(u->string());
        }
    } catch (const std::exception&) {
    }
    return "";
}

std::string logPathFor(const std::string& socketPath)
{
    if (socketPath.ends_with(".sock"))
        return socketPath.substr(0, socketPath.size() - 5) + ".log";
    return socketPath + ".log";
}

int runRelay(const RelayOptions& options, int in, int out)
{
    std::string raw, body;
    if (!readFirstMessage(in, raw, body)) return 0; // the editor left first

    std::string root = options.root;
    if (root.empty()) root = rootFromInitialize(body);
    if (root.empty()) root = fs::current_path().string();
    root = unix_socket::normalizeRoot(root);

    std::string path = options.socketPath;
    if (path.empty()) {
        if (std::string e = unix_socket::ensurePrivateDirectory(unix_socket::defaultDirectory());
            !e.empty()) {
            std::cerr << "svlsp: " << e << '\n';
            return 1;
        }
        path = unix_socket::defaultPath(root);
    }
    if (std::string e = unix_socket::checkPathLength(path); !e.empty()) {
        std::cerr << "svlsp: " << e << '\n';
        return 1;
    }

    int sock = unix_socket::connectTo(path);
    if (sock < 0) {
        const std::string log = logPathFor(path);
        {
            // Under the start lock: two editors opening the project at once
            // start one server. A server binds under the same lock, so this
            // one is released before waiting for it.
            unix_socket::StartLock lock{path};
            sock = unix_socket::connectTo(path);
            if (sock < 0) {
                std::vector<std::string> args{options.exe, "--daemon", path, "--root", root,
                                              "--no-preload"};
                args.insert(args.end(), options.serverArgs.begin(), options.serverArgs.end());
                std::cerr << "svlsp: starting a server for " << root << " on " << path
                          << " (log: " << log << ")\n";
                if (!startDetached(args, log)) {
                    std::cerr << "svlsp: can't start a server: " << std::strerror(errno) << '\n';
                    return 1;
                }
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + options.startTimeout;
        while (sock < 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            sock = unix_socket::connectTo(path);
        }
        if (sock < 0) {
            std::cerr << "svlsp: the server for " << root << " didn't start; see " << log << '\n';
            return 1;
        }
    }

    if (!writeAll(sock, raw.data(), raw.size())) {
        ::close(sock);
        return 1;
    }
    const int rc = relay(in, out, sock);
    ::close(sock);
    return rc;
}
