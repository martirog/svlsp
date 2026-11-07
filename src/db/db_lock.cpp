#include "db/db_lock.h"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <unistd.h>

bool DbLock::acquire(const std::string& dbPath, const std::string& kind, bool wait)
{
    release();
    const int fd = ::open(lockPath(dbPath).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    int rc;
    do {
        rc = ::flock(fd, LOCK_EX | (wait ? 0 : LOCK_NB));
    } while (rc != 0 && errno == EINTR);
    if (rc != 0) {
        ::close(fd);
        return false;
    }
    const std::string who = kind + " " + std::to_string(::getpid()) + "\n";
    if (::ftruncate(fd, 0) == 0) {
        [[maybe_unused]] const auto n = ::pwrite(fd, who.data(), who.size(), 0);
    }
    m_fd = fd;
    return true;
}

void DbLock::release()
{
    if (m_fd < 0) return;
    [[maybe_unused]] const int rc = ::ftruncate(m_fd, 0);
    ::flock(m_fd, LOCK_UN);
    ::close(m_fd);
    m_fd = -1;
}

DbLock::Holder DbLock::holder(const std::string& dbPath, int* pid)
{
    const int fd = ::open(lockPath(dbPath).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return Holder::None;
    // Free (we got a shared lock at once): nobody holds it.
    if (::flock(fd, LOCK_SH | LOCK_NB) == 0) {
        ::flock(fd, LOCK_UN);
        ::close(fd);
        return Holder::None;
    }
    char buf[64] = {};
    const auto n = ::pread(fd, buf, sizeof buf - 1, 0);
    ::close(fd);
    const std::string who(buf, n > 0 ? static_cast<size_t>(n) : 0);
    if (pid) {
        const auto space = who.find(' ');
        *pid = space == std::string::npos ? 0 : std::atoi(who.c_str() + space + 1);
    }
    return who.rfind("build-db", 0) == 0 ? Holder::Builder : Holder::Server;
}
