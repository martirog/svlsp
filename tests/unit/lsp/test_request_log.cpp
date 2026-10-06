#include <catch2/catch_test_macros.hpp>
#include "lsp/request_log.h"
#include <regex>
#include <sstream>

TEST_CASE("TimedLock logs one line with its wait and run time, then unlocks",
          "[request_log]")
{
    std::mutex m;
    std::ostringstream log;
    {
        TimedLock lock{m, &log, "textDocument/completion", "/p/a.sv:3:7"};
        CHECK_FALSE(m.try_lock());
    }
    CHECK(std::regex_match(log.str(),
        std::regex(R"(\[request\] textDocument/completion /p/a\.sv:3:7 wait [0-9]+\.[0-9]{3} ms, run [0-9]+\.[0-9]{3} ms\n)")));
    CHECK(m.try_lock());
    m.unlock();

    log.str("");
    { TimedLock lock{m, &log, "workspace/symbol"}; }
    CHECK(std::regex_match(log.str(),
        std::regex(R"(\[request\] workspace/symbol wait [0-9.]+ ms, run [0-9.]+ ms\n)")));
}

TEST_CASE("TimedLock without a log is a plain lock", "[request_log]")
{
    std::mutex m;
    {
        TimedLock lock{m, nullptr, "compile", "/p/a.sv"};
        CHECK_FALSE(m.try_lock());
    }
    CHECK(m.try_lock());
    m.unlock();
}
