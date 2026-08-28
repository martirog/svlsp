#include "change_debouncer.h"
#include <algorithm>
#include <vector>

ChangeDebouncer::ChangeDebouncer(Callback onFire, std::chrono::milliseconds delay)
    : m_onFire{std::move(onFire)}
    , m_delay{delay}
{
    m_worker = std::thread{&ChangeDebouncer::run, this};
}

ChangeDebouncer::~ChangeDebouncer()
{
    {
        std::lock_guard lock{m_mutex};
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void ChangeDebouncer::schedule(const std::string& key)
{
    {
        std::lock_guard lock{m_mutex};
        m_deadlines[key] = Clock::now() + m_delay;
    }
    m_cv.notify_all();
}

void ChangeDebouncer::cancel(const std::string& key)
{
    {
        std::lock_guard lock{m_mutex};
        m_deadlines.erase(key);
    }
    m_cv.notify_all();
}

void ChangeDebouncer::run()
{
    std::unique_lock lock{m_mutex};
    while (!m_stop) {
        if (m_deadlines.empty()) {
            m_cv.wait(lock, [this] { return m_stop || !m_deadlines.empty(); });
            continue;
        }

        // Wait until the earliest pending deadline. A schedule()/cancel()/stop
        // during the wait notifies us early; either way we just loop back and
        // recompute — spurious/early wakeups are harmless.
        auto earliest = std::min_element(m_deadlines.begin(), m_deadlines.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        m_cv.wait_until(lock, earliest->second);
        if (m_stop)
            break;

        // Collect and erase every due key while still holding the lock, then
        // unlock once to run the callbacks. This avoids holding a map
        // iterator across the unlocked callback calls below, where a
        // concurrent schedule()/cancel() could otherwise invalidate it.
        const auto now = Clock::now();
        std::vector<std::string> due;
        for (auto it = m_deadlines.begin(); it != m_deadlines.end();) {
            if (it->second > now) {
                ++it;
                continue;
            }
            due.push_back(it->first);
            it = m_deadlines.erase(it);
        }

        // Run callbacks without holding the lock so schedule()/cancel() from
        // another thread (or a re-schedule from within a callback itself) is
        // never blocked on a possibly-slow compile.
        lock.unlock();
        for (const auto& key : due)
            m_onFire(key);
        lock.lock();
    }
}
