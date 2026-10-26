#pragma once

#include <chrono>
#include <mutex>
#include <ostream>
#include <string>

// Holds a handler's data mutex (LanguageServer::m_dataMutex) and, with a
// request log (`svlsp --log-requests <path>`), appends one line when it
// releases it:
//
//   [request] textDocument/completion /p/a.sv:12:8 wait 0.004 ms, run 41.212 ms
//
// `wait` is the time spent acquiring the mutex (behind a background
// compile, say), `run` the time it was held -- the server's own work,
// excluding JSON (de)serialization and transport. The line is written
// before the mutex is released, so the mutex also serializes writes from
// the debounce threads. With no log it is a plain lock_guard.
class TimedLock {
public:
    using Clock = std::chrono::steady_clock;

    TimedLock(std::mutex& mutex, std::ostream* log, const char* what, std::string where = {})
        : m_mutex{mutex}, m_log{log}, m_what{what}, m_where{std::move(where)},
          m_start{Clock::now()}
    {
        m_mutex.lock();
        m_acquired = Clock::now();
    }

    ~TimedLock()
    {
        if (m_log) {
            const auto done = Clock::now();
            auto ms = [](Clock::duration d) {
                return std::chrono::duration<double, std::milli>(d).count();
            };
            *m_log << "[request] " << m_what;
            if (!m_where.empty()) *m_log << ' ' << m_where;
            *m_log << " wait " << std::fixed;
            m_log->precision(3);
            *m_log << ms(m_acquired - m_start) << " ms, run " << ms(done - m_acquired) << " ms\n";
            m_log->flush();
        }
        m_mutex.unlock();
    }

    // Report the line as if the mutex had been asked for at `start` and
    // taken at `acquired` -- for a compile, which takes it once to prepare
    // and once to commit, and is logged as one line over both.
    void reportSince(Clock::time_point start, Clock::time_point acquired)
    {
        m_start    = start;
        m_acquired = acquired;
    }

    TimedLock(const TimedLock&)            = delete;
    TimedLock& operator=(const TimedLock&) = delete;

private:
    std::mutex&       m_mutex;
    std::ostream*     m_log;
    const char*       m_what;
    std::string       m_where;
    Clock::time_point m_start;
    Clock::time_point m_acquired;
};
