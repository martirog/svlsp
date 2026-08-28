#include <catch2/catch_test_macros.hpp>
#include "lsp/change_debouncer.h"
#include <condition_variable>
#include <mutex>
#include <vector>

using namespace std::chrono_literals;

// Small real delay + generous polling timeouts, per plan.md §6.8's own note
// that the debounce scheduler "should be testable ... using a fake clock or
// a short real interval plus generous test timeouts" — no fake clock
// injection, to keep ChangeDebouncer itself simple.
namespace {

constexpr auto kDelay   = 40ms;
constexpr auto kTimeout = 2000ms; // generous upper bound for any single wait

// Thread-safe recorder of fired keys, with a condition_variable so tests can
// wait for a specific number of fires instead of polling/sleeping blindly.
class FireLog {
public:
    void record(const std::string& key)
    {
        std::lock_guard lock{m_mutex};
        m_fired.push_back(key);
        m_cv.notify_all();
    }

    // Waits until at least `count` fires have been recorded, or kTimeout
    // elapses. Returns the fired keys observed at that point.
    std::vector<std::string> waitForAtLeast(size_t count)
    {
        std::unique_lock lock{m_mutex};
        m_cv.wait_for(lock, kTimeout, [&] { return m_fired.size() >= count; });
        return m_fired;
    }

    std::vector<std::string> snapshot()
    {
        std::lock_guard lock{m_mutex};
        return m_fired;
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::vector<std::string> m_fired;
};

} // namespace

TEST_CASE("ChangeDebouncer fires once after the quiet period", "[lsp][change_debouncer]")
{
    FireLog log;
    ChangeDebouncer debouncer{[&](const std::string& key) { log.record(key); }, kDelay};

    debouncer.schedule("a.sv");

    auto fired = log.waitForAtLeast(1);
    REQUIRE(fired.size() == 1);
    REQUIRE(fired[0] == "a.sv");
}

TEST_CASE("ChangeDebouncer coalesces rapid schedule() calls into a single fire",
          "[lsp][change_debouncer]")
{
    FireLog log;
    ChangeDebouncer debouncer{[&](const std::string& key) { log.record(key); }, kDelay};

    // Several schedule() calls for the same key, each well inside the
    // debounce window — should still fire exactly once.
    for (int i = 0; i < 5; ++i) {
        debouncer.schedule("a.sv");
        std::this_thread::sleep_for(kDelay / 4);
    }

    // Wait past the last schedule()'s deadline, then confirm no extra fires
    // trickle in afterward.
    auto fired = log.waitForAtLeast(1);
    std::this_thread::sleep_for(kDelay * 2);
    REQUIRE(log.snapshot().size() == 1);
    REQUIRE(fired[0] == "a.sv");
}

TEST_CASE("ChangeDebouncer resets the deadline on a new schedule() before it fires",
          "[lsp][change_debouncer]")
{
    FireLog log;
    ChangeDebouncer debouncer{[&](const std::string& key) { log.record(key); }, kDelay};

    const auto start = std::chrono::steady_clock::now();
    debouncer.schedule("a.sv");
    std::this_thread::sleep_for(kDelay * 3 / 4); // inside the window — reset it
    debouncer.schedule("a.sv");

    auto fired = log.waitForAtLeast(1);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(fired.size() == 1);
    // Firing must not happen before the *second* schedule's own deadline —
    // i.e. total elapsed time is at least (the sleep) + kDelay, not just the
    // first schedule's kDelay.
    REQUIRE(elapsed >= kDelay * 3 / 4 + kDelay - 10ms); // small slack for scheduling jitter
}

TEST_CASE("ChangeDebouncer treats different keys independently", "[lsp][change_debouncer]")
{
    FireLog log;
    ChangeDebouncer debouncer{[&](const std::string& key) { log.record(key); }, kDelay};

    debouncer.schedule("a.sv");
    debouncer.schedule("b.sv");

    auto fired = log.waitForAtLeast(2);
    REQUIRE(fired.size() == 2);
    REQUIRE((fired[0] == "a.sv" || fired[0] == "b.sv"));
    REQUIRE((fired[1] == "a.sv" || fired[1] == "b.sv"));
    REQUIRE(fired[0] != fired[1]);
}

TEST_CASE("ChangeDebouncer cancel() prevents a pending fire", "[lsp][change_debouncer]")
{
    FireLog log;
    ChangeDebouncer debouncer{[&](const std::string& key) { log.record(key); }, kDelay};

    debouncer.schedule("a.sv");
    debouncer.cancel("a.sv");

    // Wait well past when it would have fired, then confirm it never did.
    std::this_thread::sleep_for(kDelay * 3);
    REQUIRE(log.snapshot().empty());
}

TEST_CASE("ChangeDebouncer cancel() on an unknown key is a harmless no-op",
          "[lsp][change_debouncer]")
{
    FireLog log;
    ChangeDebouncer debouncer{[&](const std::string& key) { log.record(key); }, kDelay};

    debouncer.cancel("never-scheduled.sv");
    std::this_thread::sleep_for(kDelay * 2);
    REQUIRE(log.snapshot().empty());
}
