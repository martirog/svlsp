#include "lsp/unix_socket.h"
#include "compiler/content_hash.h"
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace unix_socket {

namespace {

std::string errnoText(const std::string& what)
{
    return what + ": " + std::strerror(errno);
}

bool fillAddress(const std::string& path, sockaddr_un& addr)
{
    if (path.size() > kMaxPathLength) return false;
    std::memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.data(), path.size());
    return true;
}

} // namespace

std::string defaultDirectory()
{
    return "/tmp/svlsp-" + std::to_string(::getuid());
}

std::string normalizeRoot(const std::string& root)
{
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(root), ec);
    if (ec) p = fs::absolute(root).lexically_normal();
    std::string s = p.string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string defaultPath(const std::string& root)
{
    return defaultDirectory() + "/" + contentHash(normalizeRoot(root)).substr(0, 16) + ".sock";
}

std::string ensurePrivateDirectory(const std::string& dir)
{
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        return errnoText("can't create '" + dir + "'");
    struct stat st{};
    if (::lstat(dir.c_str(), &st) != 0) return errnoText("can't stat '" + dir + "'");
    if (!S_ISDIR(st.st_mode)) return "'" + dir + "' is not a directory";
    if (st.st_uid != ::getuid()) return "'" + dir + "' belongs to another user";
    if ((st.st_mode & 077) != 0)
        return "'" + dir + "' is open to other users (mode " +
               std::to_string((st.st_mode >> 6) & 7) + std::to_string((st.st_mode >> 3) & 7) +
               std::to_string(st.st_mode & 7) + "; it must be 700)";
    return "";
}

std::string checkPathLength(const std::string& path)
{
    if (path.size() <= kMaxPathLength) return "";
    return "socket path '" + path + "' is " + std::to_string(path.size()) +
           " bytes; a Unix socket path can have at most " + std::to_string(kMaxPathLength);
}

void FdStream::read(char* buffer, std::size_t size)
{
    while (size > 0) {
        if (m_pos == m_len) {
            ssize_t n;
            do n = ::recv(m_fd, m_buf, sizeof m_buf, 0);
            while (n < 0 && errno == EINTR);
            if (n == 0) throw lsp::io::Error{"connection closed"};
            if (n < 0) throw lsp::io::Error{errnoText("read")};
            m_pos = 0;
            m_len = static_cast<std::size_t>(n);
        }
        const std::size_t take = std::min(size, m_len - m_pos);
        std::memcpy(buffer, m_buf + m_pos, take);
        m_pos += take;
        buffer += take;
        size -= take;
    }
}

void FdStream::write(const char* buffer, std::size_t size)
{
    while (size > 0) {
        const ssize_t n = ::send(m_fd, buffer, size, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw lsp::io::Error{errnoText("write")};
        }
        buffer += n;
        size -= static_cast<std::size_t>(n);
    }
}

std::optional<Peer> peerOf(int fd)
{
    ucred cred{};
    socklen_t len = sizeof cred;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return std::nullopt;
    return Peer{.pid = cred.pid, .uid = cred.uid};
}

int connectTo(const std::string& path)
{
    sockaddr_un addr{};
    if (!fillAddress(path, addr)) return -1;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int rc;
    do rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

StartLock::StartLock(const std::string& socketPath)
{
    m_fd = ::open((socketPath + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_fd < 0) return;
    int rc;
    do rc = ::flock(m_fd, LOCK_EX);
    while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

StartLock::~StartLock()
{
    if (m_fd >= 0) ::close(m_fd); // releases the flock
}

Listener::~Listener()
{
    close();
}

std::string Listener::bind(const std::string& path)
{
    if (std::string e = checkPathLength(path); !e.empty()) return e;
    StartLock lock{path};
    if (!lock.held()) return errnoText("can't lock '" + path + ".lock'");

    struct stat st{};
    if (::lstat(path.c_str(), &st) == 0) {
        if (!S_ISSOCK(st.st_mode)) return "'" + path + "' exists and is not a socket";
        if (const int fd = connectTo(path); fd >= 0) {
            ::close(fd);
            return "another svlsp already listens on '" + path + "'";
        }
        // Stale: left by a server that died.
        if (::unlink(path.c_str()) != 0) return errnoText("can't remove stale '" + path + "'");
    }

    sockaddr_un addr{};
    fillAddress(path, addr);
    m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_fd < 0) return errnoText("socket");
    // The file gets 0777 less the umask: create it private, then make sure.
    const mode_t old = ::umask(077);
    const int rc = ::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::umask(old);
    if (rc != 0) {
        const std::string e = errnoText("can't bind '" + path + "'");
        ::close(m_fd);
        m_fd = -1;
        return e;
    }
    m_path = path;
    if (::chmod(path.c_str(), 0600) != 0 || ::listen(m_fd, 8) != 0 ||
        ::stat(path.c_str(), &st) != 0) {
        const std::string e = errnoText("can't listen on '" + path + "'");
        close();
        return e;
    }
    m_dev = st.st_dev;
    m_ino = st.st_ino;
    return "";
}

int Listener::accept(std::chrono::milliseconds timeout)
{
    if (m_fd < 0) return -1;
    pollfd p{.fd = m_fd, .events = POLLIN, .revents = 0};
    if (::poll(&p, 1, static_cast<int>(timeout.count())) <= 0) return -1;
    return ::accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
}

bool Listener::stillOurs() const
{
    if (m_path.empty()) return false;
    struct stat st{};
    return ::lstat(m_path.c_str(), &st) == 0 && st.st_dev == m_dev && st.st_ino == m_ino;
}

void Listener::close()
{
    if (m_fd < 0) return;
    if (stillOurs()) ::unlink(m_path.c_str());
    ::close(m_fd);
    m_fd = -1;
}

std::optional<std::chrono::seconds> parseDuration(const std::string& text)
{
    if (text.empty()) return std::nullopt;
    std::size_t i = 0;
    unsigned long long n = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        n = n * 10 + static_cast<unsigned>(text[i] - '0');
        if (n > 100ULL * 365 * 86400) return std::nullopt;
        ++i;
    }
    if (i == 0) return std::nullopt;
    unsigned long long unit = 1;
    if (i < text.size()) {
        if (i + 1 != text.size()) return std::nullopt;
        switch (text[i]) {
        case 's': unit = 1; break;
        case 'm': unit = 60; break;
        case 'h': unit = 3600; break;
        case 'd': unit = 86400; break;
        default: return std::nullopt;
        }
    }
    return std::chrono::seconds{static_cast<long long>(n * unit)};
}

} // namespace unix_socket
