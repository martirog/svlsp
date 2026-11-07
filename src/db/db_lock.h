#pragma once
#include <string>

// The one-writer-at-a-time lock of a project DB file (plan.md §6.34 S3):
// `flock` on `<db>.lock`, held for the writer's whole life -- a server, or a
// `--build-db`. The lock file says who holds it ("server <pid>" or
// "build-db <pid>"), so the next one can tell which case it is in. The
// kernel drops the lock when the holder exits, however it exits.
class DbLock {
public:
    enum class Holder { None, Server, Builder };

    DbLock() = default;
    ~DbLock() { release(); }
    DbLock(const DbLock&)            = delete;
    DbLock& operator=(const DbLock&) = delete;

    // Takes the lock on `dbPath` for a holder of `kind` ("server" or
    // "build-db"), waiting for it when `wait`, else failing at once.
    bool acquire(const std::string& dbPath, const std::string& kind, bool wait = false);
    void release();
    bool held() const { return m_fd >= 0; }

    // Who holds `dbPath`'s lock now (None if nobody), and their pid.
    static Holder holder(const std::string& dbPath, int* pid = nullptr);

    static std::string lockPath(const std::string& dbPath) { return dbPath + ".lock"; }

private:
    int m_fd{-1};
};
