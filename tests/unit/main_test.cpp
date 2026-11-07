// Catch2 provides its own main when linking against Catch2::Catch2WithMain.
// This file holds what the whole run shares.
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {

// A project without a "database" gets one in the XDG cache (plan.md §6.34
// S3b): the run's own, fresh each run, never the user's ~/.cache.
class IsolatedCache : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;
    void testRunStarting(const Catch::TestRunInfo&) override
    {
        m_dir = "/tmp/svlsp_test_xdg_" + std::to_string(::getpid());
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
        ::setenv("XDG_CACHE_HOME", m_dir.c_str(), 1);
    }
    void testRunEnded(const Catch::TestRunStats&) override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }

private:
    std::string m_dir;
};

} // namespace

CATCH_REGISTER_LISTENER(IsolatedCache)
