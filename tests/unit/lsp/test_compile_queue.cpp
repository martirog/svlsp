// CompileQueue (plan.md §6.34 S4) and the load-order pre-scan.
#include <catch2/catch_test_macros.hpp>
#include "lsp/compile_queue.h"
#include "lsp/load_order.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace {

// A queue whose first job blocks until released, so the rest can be pushed
// while it runs.
struct Recorder {
    std::mutex m;
    std::condition_variable cv;
    bool released{false}, started{false};
    std::vector<std::string> ran;
    std::vector<CompileQueue::Job> jobs;
    CompileQueue queue{[this](const CompileQueue::Job& job) {
        std::unique_lock l{m};
        if (job.key == "block") {
            started = true;
            cv.notify_all();
            cv.wait(l, [this] { return released; });
            return;
        }
        ran.push_back(job.key);
        jobs.push_back(job);
    }};
    void block()
    {
        queue.push({.key = "block"}, 0);
        std::unique_lock l{m};
        cv.wait(l, [this] { return started; });
    }
    void release()
    {
        {
            std::lock_guard l{m};
            released = true;
        }
        cv.notify_all();
        queue.waitIdle();
    }
};

} // namespace

TEST_CASE("compile queue: lower ranks first, first come first served within a rank",
          "[lsp][queue][s4]")
{
    Recorder r;
    r.block();
    r.queue.push({.key = "rest1"}, CompileQueue::kRest);
    r.queue.push({.key = "open"}, CompileQueue::kOpenDocument);
    r.queue.push({.key = "rest2"}, CompileQueue::kRest);
    r.queue.push({.key = "finish", .publishAll = false}, CompileQueue::kProjectFinish);
    r.queue.push({.key = "unit"}, CompileQueue::kOpenUnit);
    r.release();
    CHECK(r.ran == std::vector<std::string>{"unit", "open", "rest1", "rest2", "finish"});
}

TEST_CASE("compile queue: a job pushed twice runs once, at the better rank, flags merged",
          "[lsp][queue][s4]")
{
    Recorder r;
    r.block();
    r.queue.push({.key = "a", .openOnly = false, .publishAll = false, .project = "/p.json"},
                 CompileQueue::kRest);
    r.queue.push({.key = "b"}, CompileQueue::kOpenDocument);
    r.queue.push({.key = "a", .force = true}, CompileQueue::kOpenDocument);
    // Another kind with the same key is another job.
    r.queue.push({.kind = CompileQueue::Job::Kind::FinishProject, .key = "a"},
                 CompileQueue::kProjectFinish);
    r.release();
    REQUIRE(r.ran == std::vector<std::string>{"b", "a", "a"});
    const auto& a = r.jobs[1];
    CHECK(a.kind == CompileQueue::Job::Kind::Unit);
    CHECK(a.force);
    CHECK_FALSE(a.openOnly);
    CHECK(a.publishAll);
    CHECK(a.project == "/p.json");
    CHECK(r.jobs[2].kind == CompileQueue::Job::Kind::FinishProject);
}

TEST_CASE("load-order pre-scan: declarations, references and includes",
          "[lsp][queue][s4]")
{
    const auto s = scanSource(
        "// package not_this;\n"
        "package agent_pkg;\n"
        "  import uvm_pkg::*;\n"
        "  `include \"uvm_macros.svh\"\n"
        "  `include \"agents/agent_driver.svh\"\n"
        "  virtual class base_c extends uvm_object;\n"
        "  endclass\n"
        "  interface class if_c; endclass\n"
        "  class d_c extends base_c implements if_c, other_if;\n"
        "    cfg_pkg::cfg_t c; /* class hidden_c; */\n"
        "    string s = \"class in_string\";\n"
        "  endclass\n"
        "endpackage\n"
        "module top; endmodule\n");
    CHECK(s.declares == std::vector<std::string>{"agent_pkg", "base_c", "if_c", "d_c", "top"});
    CHECK(s.references ==
          std::vector<std::string>{"uvm_pkg", "uvm_object", "base_c", "if_c", "other_if", "cfg_pkg"});
    CHECK(s.includes == std::vector<std::string>{"uvm_macros.svh", "agents/agent_driver.svh"});
}
