#include "lsp/socket_daemon.h"
#include <lsp/connection.h>
#include <lsp/error.h>
#include <lsp/jsonrpc/jsonrpc.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <ostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

std::string timestamp()
{
    const std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;
}

std::string durationText(std::chrono::seconds s)
{
    const auto n = s.count();
    if (n % 86400 == 0 && n > 2 * 86400) return std::to_string(n / 86400) + "d";
    if (n % 3600 == 0) return std::to_string(n / 3600) + "h";
    if (n % 60 == 0) return std::to_string(n / 60) + "m";
    return std::to_string(n) + "s";
}

sigset_t stopSignals()
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGHUP);
    return set;
}

} // namespace

SocketDaemon::SocketDaemon(Options options, Session session)
    : m_options{std::move(options)}
    , m_session{std::move(session)}
{
}

SocketDaemon::~SocketDaemon()
{
    for (auto& t : m_waiters)
        if (t.joinable()) t.join();
}

void SocketDaemon::blockStopSignals()
{
    const sigset_t set = stopSignals();
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

void SocketDaemon::logLine(const std::string& line)
{
    if (!m_options.log) return;
    std::lock_guard lock{m_logMutex};
    *m_options.log << timestamp() << " [daemon] " << line << std::endl;
}

void SocketDaemon::refuse(int fd, const std::string& message, std::chrono::milliseconds timeout)
{
    timeval tv{.tv_sec = static_cast<time_t>(timeout.count() / 1000),
               .tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    try {
        unix_socket::FdStream stream{fd};
        lsp::Connection conn{stream};
        auto msg = conn.readMessage();
        auto* single = std::get_if<lsp::jsonrpc::Message>(&msg);
        if (!single) return;
        auto* request = std::get_if<lsp::jsonrpc::Request>(single);
        if (!request || !request->id) return;
        conn.writeMessage(lsp::jsonrpc::Message{lsp::jsonrpc::createErrorResponse(
            *request->id, lsp::MessageError::RequestFailed, message)});
    } catch (const std::exception&) {
        // A peer that sent nothing in time, or garbage: just closed.
    }
}

void SocketDaemon::stop(const std::string& why)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_stop) return;
        m_stopWhy = why;
        m_stop    = true;
        // The session's read fails: LanguageServer::run returns.
        if (m_sessionFd >= 0) ::shutdown(m_sessionFd, SHUT_RDWR);
    }
    m_cv.notify_all();
}

void SocketDaemon::startPreload()
{
    if (m_options.preload.empty()) return;
    std::vector<char*> argv;
    for (auto& a : m_options.preload) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        logLine(std::string{"can't start the preload: "} + std::strerror(errno));
        return;
    }
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        pthread_sigmask(SIG_SETMASK, &none, nullptr);
        const int null = ::open("/dev/null", O_RDONLY);
        if (null >= 0) ::dup2(null, 0);
        ::dup2(2, 1);
        ::execv(argv[0], argv.data());
        _exit(127);
    }
    m_preloadPid = pid;
    std::string cmd;
    for (auto& a : m_options.preload) cmd += (cmd.empty() ? "" : " ") + a;
    logLine("preloading the project: pid " + std::to_string(pid) + ": " + cmd);
}

void SocketDaemon::reapPreload(bool wait)
{
    if (m_preloadPid <= 0) return;
    if (wait) ::kill(m_preloadPid, SIGTERM);
    int status = 0;
    const pid_t r = ::waitpid(m_preloadPid, &status, wait ? 0 : WNOHANG);
    if (r != m_preloadPid) return;
    if (WIFEXITED(status))
        logLine("preload done (exit " + std::to_string(WEXITSTATUS(status)) + ")");
    else
        logLine("preload ended by signal " + std::to_string(WTERMSIG(status)));
    m_preloadPid = 0;
}

void SocketDaemon::signalLoop()
{
    const sigset_t set = stopSignals();
    while (!m_stop) {
        const timespec ts{.tv_sec = 0, .tv_nsec = 200'000'000};
        const int sig = ::sigtimedwait(&set, nullptr, &ts);
        if (sig > 0) stop(std::string{"signal "} + ::strsignal(sig));
    }
}

void SocketDaemon::acceptLoop()
{
    auto lastOrphanCheck = std::chrono::steady_clock::now();
    while (!m_stop) {
        const int fd = m_listener.accept(m_options.pollInterval);
        reapPreload(false);
        const auto now = std::chrono::steady_clock::now();
        if (now - lastOrphanCheck >= m_options.orphanCheckInterval) {
            lastOrphanCheck = now;
            if (!m_listener.stillOurs()) {
                stop("the socket file '" + m_options.socketPath + "' was removed");
                break;
            }
        }
        if (fd < 0) {
            std::lock_guard lock{m_mutex};
            if (m_options.idleTimeout.count() > 0 && m_sessionFd < 0 && m_pending < 0 &&
                now - m_idleSince >= m_options.idleTimeout) {
                m_stopWhy = "no editor attached for " + durationText(m_options.idleTimeout);
                m_stop    = true;
                m_cv.notify_all();
            }
            continue;
        }

        const auto peer = unix_socket::peerOf(fd);
        if (!peer || peer->uid != ::getuid()) {
            logLine("refused a connection from uid " +
                    (peer ? std::to_string(peer->uid) : std::string{"?"}));
            ::close(fd);
            continue;
        }
        std::unique_lock lock{m_mutex};
        if (m_sessionFd >= 0 || m_pending >= 0) {
            lock.unlock();
            m_waiters.emplace_back([this, fd, pid = peer->pid] { waitOrRefuse(fd, pid); });
            continue;
        }
        m_pending = fd;
        lock.unlock();
        m_cv.notify_all();
    }
}

void SocketDaemon::waitOrRefuse(int fd, pid_t pid)
{
    {
        std::unique_lock lock{m_mutex};
        if (m_cv.wait_for(lock, m_options.busyGrace,
                          [this] { return m_stop || (m_sessionFd < 0 && m_pending < 0); }) &&
            !m_stop) {
            m_pending = fd;
            lock.unlock();
            m_cv.notify_all();
            return;
        }
        if (m_stop) {
            ::close(fd);
            return;
        }
    }
    logLine("refused pid " + std::to_string(pid) + ": another editor is attached");
    refuse(fd,
           "svlsp: the server for " + m_options.root + " is in use by another editor",
           std::chrono::milliseconds{5000});
    ::close(fd);
}

int SocketDaemon::run()
{
    if (std::string e = m_listener.bind(m_options.socketPath); !e.empty()) {
        logLine(e);
        return 1;
    }
    m_idleSince = std::chrono::steady_clock::now();
    logLine("listening on " + m_options.socketPath + " (pid " + std::to_string(::getpid()) +
            ") for " + m_options.root + "; idle timeout " +
            (m_options.idleTimeout.count() > 0 ? durationText(m_options.idleTimeout)
                                               : std::string{"none"}));
    startPreload();

    std::thread signals;
    if (m_options.handleSignals) signals = std::thread{[this] { signalLoop(); }};
    std::thread acceptor{[this] { acceptLoop(); }};

    while (true) {
        int fd = -1;
        {
            std::unique_lock lock{m_mutex};
            m_cv.wait(lock, [this] { return m_stop || m_pending >= 0; });
            if (m_stop) {
                if (m_pending >= 0) ::close(m_pending);
                m_pending = -1;
                break;
            }
            fd            = m_pending;
            m_pending     = -1;
            m_sessionFd   = fd;
        }
        const auto peer = unix_socket::peerOf(fd).value_or(unix_socket::Peer{});
        logLine("editor attached (connection from pid " + std::to_string(peer.pid) + ")");
        try {
            unix_socket::FdStream stream{fd};
            m_session(stream, peer);
        } catch (const std::exception& e) {
            logLine(std::string{"session error: "} + e.what());
        }
        {
            std::lock_guard lock{m_mutex};
            m_sessionFd = -1;
            ::close(fd);
            m_idleSince = std::chrono::steady_clock::now();
        }
        m_cv.notify_all(); // a waiting connection takes the slot
        std::string line = "editor detached (pid " + std::to_string(peer.pid) + ")";
        if (m_options.idleTimeout.count() > 0 && !m_stop)
            line += "; exits in " + durationText(m_options.idleTimeout) + " if none attaches";
        logLine(line);
    }

    acceptor.join(); // within pollInterval: m_stop is set
    if (signals.joinable()) signals.join();
    m_listener.close();
    reapPreload(true);
    logLine("stopped: " + m_stopWhy);
    return 0;
}
