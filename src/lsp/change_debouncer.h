#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

// Coalesces rapid-fire schedule() calls for the same key into a single
// deferred callback invocation, fired once no further schedule() for that
// key arrives within `delay` — the debounce worker design from
// plan.md §6.8. Intended use: LanguageServer calls schedule(uri) from every
// didChange notification instead of compiling immediately; ChangeDebouncer's
// own background thread invokes the fire callback once typing pauses, well
// off the LSP message-read thread.
//
// One dedicated worker thread serves every key. schedule()/cancel() are
// O(log n) map operations plus a condition_variable notify — safe to call
// from the message-read thread on every keystroke.
class ChangeDebouncer {
public:
    using Callback = std::function<void(const std::string& key)>;

    explicit ChangeDebouncer(Callback onFire,
        std::chrono::milliseconds delay = std::chrono::milliseconds{300});
    ~ChangeDebouncer();

    ChangeDebouncer(const ChangeDebouncer&)            = delete;
    ChangeDebouncer& operator=(const ChangeDebouncer&) = delete;

    // Schedules `key` to fire after `delay` of no further schedule() call
    // for that key. Resets the deadline if already pending.
    void schedule(const std::string& key);

    // Cancels any pending fire for `key` (no-op if none pending).
    void cancel(const std::string& key);

private:
    using Clock = std::chrono::steady_clock;

    Callback                                 m_onFire;
    std::chrono::milliseconds                m_delay;
    std::map<std::string, Clock::time_point> m_deadlines;
    std::mutex                               m_mutex;
    std::condition_variable                  m_cv;
    bool                                     m_stop{false};
    std::thread                              m_worker;

    void run();
};
