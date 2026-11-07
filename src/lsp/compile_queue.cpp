#include "lsp/compile_queue.h"
#include <exception>
#include <iostream>

CompileQueue::CompileQueue(std::function<void(const Job&)> run)
    : m_run(std::move(run))
    , m_thread([this] { loop(); })
{}

CompileQueue::~CompileQueue()
{
    {
        std::lock_guard l{m_mutex};
        m_stop = true;
        m_jobs.clear();
        m_order.clear();
        m_projectJobs = 0;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void CompileQueue::push(Job job, int rank)
{
    {
        std::lock_guard l{m_mutex};
        if (m_stop) return;
        const Id id{static_cast<int>(job.kind), job.key};
        auto it = m_jobs.find(id);
        if (it == m_jobs.end()) {
            if (job.kind != Job::Kind::Unit) ++m_projectJobs;
            m_order.emplace(rank, m_nextSeq, id);
            m_jobs.emplace(id, Entry{std::move(job), rank, m_nextSeq++});
        } else {
            Entry& e = it->second;
            e.job.force      = e.job.force || job.force;
            e.job.openOnly   = e.job.openOnly && job.openOnly;
            e.job.publishAll = e.job.publishAll || job.publishAll;
            if (e.job.project.empty()) e.job.project = std::move(job.project);
            if (rank < e.rank) {
                m_order.erase({e.rank, e.seq, id});
                e.rank = rank;
                e.seq  = m_nextSeq++;
                m_order.emplace(e.rank, e.seq, id);
            }
        }
    }
    m_cv.notify_all();
}

void CompileQueue::waitIdle()
{
    std::unique_lock l{m_mutex};
    m_cv.wait(l, [this] { return m_stop || (m_jobs.empty() && !m_running); });
}

size_t CompileQueue::pending() const
{
    std::lock_guard l{m_mutex};
    return m_jobs.size();
}

void CompileQueue::loop()
{
    std::unique_lock l{m_mutex};
    while (true) {
        m_cv.wait(l, [this] { return m_stop || !m_jobs.empty(); });
        if (m_stop) return;
        const Id id = std::get<2>(*m_order.begin());
        m_order.erase(m_order.begin());
        auto it = m_jobs.find(id);
        Job job = std::move(it->second.job);
        m_jobs.erase(it);
        m_running = true;
        m_runningProject = job.kind != Job::Kind::Unit;
        if (m_runningProject) --m_projectJobs;
        l.unlock();
        try {
            m_run(job);
        } catch (const std::exception& e) {
            std::cerr << "svlsp: background job failed: " << e.what() << '\n';
        }
        l.lock();
        m_running = false;
        m_runningProject = false;
        m_cv.notify_all();
    }
}

bool CompileQueue::projectWork() const
{
    std::lock_guard l{m_mutex};
    return m_projectJobs > 0 || m_runningProject;
}
