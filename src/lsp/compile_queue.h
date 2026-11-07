#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>

// The server's background compile queue (plan.md §6.34 S4): one worker
// thread runs every compile, project load and refresh step, in rank order
// (lower first), first come first served within a rank. A job is identified
// by its kind and key; pushing one that is already queued merges the two
// (the better rank, and the flags combined) instead of queueing it twice.
class CompileQueue {
public:
    struct Job {
        enum class Kind { Unit, LoadProject, FinishProject, Refresh };
        Kind        kind{Kind::Unit};
        std::string key;            // the unit's path, or the project's config path
        bool        force{false};   // Unit: bypass the cache (merged: either)
        bool        openOnly{true}; // Unit: only an open document's text (merged: both)
        bool        publishAll{true}; // Unit: publish files never published (merged: either)
        std::string project{};      // Unit: the config path it compiles under ("" = discover)
    };

    // The ranks the server uses.
    static constexpr int kProjectSetup   = 0; // a project's load / refresh prologue
    static constexpr int kOpenUnit       = 1; // the units that include an open document
    static constexpr int kOpenDocument   = 2; // open documents (didOpen, didChange, didSave)
    static constexpr int kDeclarers      = 3; // units declaring what open documents name
    static constexpr int kRest           = 4; // the rest of a load, and save cascades
    static constexpr int kProjectFinish  = 5; // a load's or refresh's last step

    explicit CompileQueue(std::function<void(const Job&)> run);
    ~CompileQueue(); // drops what is still queued, waits for the running job

    CompileQueue(const CompileQueue&)            = delete;
    CompileQueue& operator=(const CompileQueue&) = delete;

    void push(Job job, int rank);

    // Blocks until nothing is queued or running (tests).
    void waitIdle();

    // Jobs queued, not counting the one running.
    size_t pending() const;

    // Whether a project job (a load, a refresh, a load's last step) is
    // queued or running.
    bool projectWork() const;

private:
    struct Entry {
        Job job;
        int rank;
        uint64_t seq;
    };
    using Id = std::pair<int, std::string>; // (kind, key)

    std::function<void(const Job&)> m_run;
    mutable std::mutex              m_mutex;
    std::condition_variable         m_cv;
    std::map<Id, Entry>             m_jobs;
    std::set<std::tuple<int, uint64_t, Id>> m_order; // (rank, seq, id) of each of m_jobs
    uint64_t                        m_nextSeq{0};
    bool                            m_running{false};
    bool                            m_runningProject{false};
    size_t                          m_projectJobs{0}; // queued non-Unit jobs
    bool                            m_stop{false};
    std::thread                     m_thread;

    void loop();
};
