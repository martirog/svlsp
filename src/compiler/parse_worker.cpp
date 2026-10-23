#include "compiler/parse_worker.h"
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <ostream>
#include <spawn.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace {

// ---- Wire encoding ----

class Writer {
public:
    void u8(uint8_t v) { m_buf.push_back(static_cast<char>(v)); }
    void u32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i) m_buf.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
    void i32(int v) { u32(static_cast<uint32_t>(v)); }
    void str(const std::string& s)
    {
        u32(static_cast<uint32_t>(s.size()));
        m_buf += s;
    }
    std::string take() { return std::move(m_buf); }

private:
    std::string m_buf;
};

class Reader {
public:
    explicit Reader(const std::string& buf) : m_buf{buf} {}
    bool ok() const { return m_ok; }
    bool atEnd() const { return m_pos == m_buf.size(); }

    uint8_t u8()
    {
        if (!need(1)) return 0;
        return static_cast<uint8_t>(m_buf[m_pos++]);
    }
    uint32_t u32()
    {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(static_cast<uint8_t>(m_buf[m_pos++])) << (8 * i);
        return v;
    }
    int i32() { return static_cast<int>(u32()); }
    std::string str()
    {
        const uint32_t n = u32();
        if (!need(n)) return {};
        std::string s = m_buf.substr(m_pos, n);
        m_pos += n;
        return s;
    }
    // A count of elements that each take at least `minBytes`, checked
    // against what is left so a corrupt count can't allocate gigabytes.
    uint32_t count(std::size_t minBytes)
    {
        const uint32_t n = u32();
        if (m_ok && n > (m_buf.size() - m_pos) / minBytes) m_ok = false;
        return m_ok ? n : 0;
    }

private:
    const std::string& m_buf;
    std::size_t        m_pos{0};
    bool               m_ok{true};

    bool need(std::size_t n)
    {
        if (!m_ok || m_buf.size() - m_pos < n) m_ok = false;
        return m_ok;
    }
};

void put(Writer& w, const SourceLine& l)
{
    w.str(l.file);
    w.i32(l.line);
    w.u32(static_cast<uint32_t>(l.colShifts.size()));
    for (const auto& c : l.colShifts) { w.i32(c.outputCol); w.i32(c.delta); }
}

void get(Reader& r, SourceLine& l)
{
    l.file = r.str();
    l.line = r.i32();
    l.colShifts.resize(r.count(8));
    for (auto& c : l.colShifts) { c.outputCol = r.i32(); c.delta = r.i32(); }
}

void put(Writer& w, const ParseRecord& p)
{
    w.u8(static_cast<uint8_t>(p.kind));
    w.str(p.name);
    w.i32(p.line);
    w.i32(p.column);
    w.str(p.parent);
    w.str(p.detail);
    w.i32(p.endLine);
    w.str(p.scope);
    w.str(p.file);
    w.str(p.doc);
    w.str(p.access);
    w.i32(p.endCol);
}

void get(Reader& r, ParseRecord& p)
{
    p.kind    = static_cast<ParseRecordKind>(r.u8());
    p.name    = r.str();
    p.line    = r.i32();
    p.column  = r.i32();
    p.parent  = r.str();
    p.detail  = r.str();
    p.endLine = r.i32();
    p.scope   = r.str();
    p.file    = r.str();
    p.doc     = r.str();
    p.access  = r.str();
    p.endCol  = r.i32();
}

void put(Writer& w, const ParseError& e)
{
    w.i32(e.line);
    w.i32(e.column);
    w.str(e.message);
    w.str(e.file);
    w.i32(e.severity);
}

void get(Reader& r, ParseError& e)
{
    e.line     = r.i32();
    e.column   = r.i32();
    e.message  = r.str();
    e.file     = r.str();
    e.severity = r.i32();
}

void put(Writer& w, const ImportRecord& i)
{
    w.str(i.pkgName);
    w.str(i.item);
    w.i32(i.line);
    w.str(i.file);
    w.u8(i.isExport ? 1 : 0);
    w.str(i.scope);
}

void get(Reader& r, ImportRecord& i)
{
    i.pkgName  = r.str();
    i.item     = r.str();
    i.line     = r.i32();
    i.file     = r.str();
    i.isExport = r.u8() != 0;
    i.scope    = r.str();
}

void put(Writer& w, const InstantiationRecord& i)
{
    w.str(i.typeName);
    w.str(i.instName);
    w.i32(i.line);
    w.str(i.file);
}

void get(Reader& r, InstantiationRecord& i)
{
    i.typeName = r.str();
    i.instName = r.str();
    i.line     = r.i32();
    i.file     = r.str();
}

void put(Writer& w, const CallRecord& c)
{
    w.str(c.calleeName);
    w.str(c.calleeScope);
    w.u32(static_cast<uint32_t>(c.args.size()));
    for (const auto& a : c.args) { w.u8(static_cast<uint8_t>(a.kind)); w.str(a.name); }
    w.i32(c.line);
    w.i32(c.column);
    w.str(c.file);
    w.i32(c.nameLine);
    w.i32(c.nameColumn);
}

void get(Reader& r, CallRecord& c)
{
    c.calleeName  = r.str();
    c.calleeScope = r.str();
    c.args.resize(r.count(5));
    for (auto& a : c.args) {
        a.kind = static_cast<CallArgSlot::Kind>(r.u8());
        a.name = r.str();
    }
    c.line       = r.i32();
    c.column     = r.i32();
    c.file       = r.str();
    c.nameLine   = r.i32();
    c.nameColumn = r.i32();
}

template <class T>
void putAll(Writer& w, const std::vector<T>& v)
{
    w.u32(static_cast<uint32_t>(v.size()));
    for (const auto& x : v) put(w, x);
}

template <class T>
void getAll(Reader& r, std::vector<T>& v)
{
    v.resize(r.count(4));
    for (auto& x : v) get(r, x);
}

// ---- Framing: a u32 length, then the payload ----

bool writeAll(int fd, const char* data, std::size_t n)
{
    while (n > 0) {
        // MSG_NOSIGNAL: a worker that died is an error to handle, not a
        // SIGPIPE that kills the server.
        const ssize_t k = ::send(fd, data, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        data += k;
        n -= static_cast<std::size_t>(k);
    }
    return true;
}

bool readAll(int fd, char* data, std::size_t n)
{
    while (n > 0) {
        const ssize_t k = ::read(fd, data, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        data += k;
        n -= static_cast<std::size_t>(k);
    }
    return true;
}

bool writeFrame(int fd, const std::string& payload)
{
    Writer w;
    w.u32(static_cast<uint32_t>(payload.size()));
    const std::string header = w.take();
    return writeAll(fd, header.data(), header.size()) &&
           writeAll(fd, payload.data(), payload.size());
}

// false on EOF or a short read.
bool readFrame(int fd, std::string& payload)
{
    char header[4];
    if (!readAll(fd, header, sizeof header)) return false;
    const std::string h(header, sizeof header);
    Reader r{h};
    payload.resize(r.u32());
    return readAll(fd, payload.data(), payload.size());
}

// VmRSS of `pid` in MB, 0 when unreadable.
std::size_t rssMb(pid_t pid)
{
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind("VmRSS:", 0) == 0) return std::stoul(line.substr(6)) / 1024;
    return 0;
}

} // namespace

std::string encodeWalkRequest(const WalkRequest& req)
{
    Writer w;
    w.str(req.source);
    putAll(w, req.sourceMap);
    w.u8(req.collectDocs ? 1 : 0);
    w.str(req.initialScope);
    return w.take();
}

bool decodeWalkRequest(const std::string& buf, WalkRequest& out)
{
    Reader r{buf};
    out.source = r.str();
    getAll(r, out.sourceMap);
    out.collectDocs  = r.u8() != 0;
    out.initialScope = r.str();
    return r.ok() && r.atEnd();
}

std::string encodeWalkResult(const WalkResult& result)
{
    Writer w;
    putAll(w, result.records);
    putAll(w, result.parseErrors);
    putAll(w, result.imports);
    putAll(w, result.instantiations);
    putAll(w, result.calls);
    w.u32(static_cast<uint32_t>(result.includeScopes.size()));
    for (const auto& [file, scope] : result.includeScopes) { w.str(file); w.str(scope); }
    return w.take();
}

bool decodeWalkResult(const std::string& buf, WalkResult& out)
{
    Reader r{buf};
    getAll(r, out.records);
    getAll(r, out.parseErrors);
    getAll(r, out.imports);
    getAll(r, out.instantiations);
    getAll(r, out.calls);
    out.includeScopes.clear();
    for (uint32_t n = r.count(8); n > 0 && r.ok(); --n) {
        std::string file = r.str();
        out.includeScopes[std::move(file)] = r.str();
    }
    return r.ok() && r.atEnd();
}

// ---- ParseWorker ----

ParseWorker::ParseWorker(Options options) : m_options{std::move(options)}
{
    if (m_options.maxRssMb > 0 && m_options.idleSeconds > 0)
        m_reaper = std::thread{&ParseWorker::reap, this};
}

ParseWorker::~ParseWorker()
{
    {
        std::lock_guard lock{m_mutex};
        m_stopping = true;
    }
    m_cv.notify_all();
    if (m_reaper.joinable()) m_reaper.join();
    std::lock_guard lock{m_mutex};
    stop();
}

void ParseWorker::reap()
{
    std::unique_lock lock{m_mutex};
    while (!m_stopping) {
        if (!m_overLimit) {
            m_cv.wait(lock, [&] { return m_stopping || m_overLimit; });
            continue;
        }
        // Re-read on every wake-up: a walk in between clears m_overLimit and
        // re-marks it with a new m_idleSince when it ends.
        const auto deadline = m_idleSince + std::chrono::seconds{m_options.idleSeconds};
        if (std::chrono::steady_clock::now() < deadline) {
            m_cv.wait_until(lock, deadline);
            continue;
        }
        log("pid " + std::to_string(m_pid) + " idle " + std::to_string(m_options.idleSeconds) +
            " s, recycled");
        m_overLimit = false;
        stop();
    }
}

pid_t ParseWorker::pid() const
{
    std::lock_guard lock{m_mutex};
    return m_pid;
}

void ParseWorker::log(const std::string& line) const
{
    if (!m_options.log) return;
    *m_options.log << "[worker] " << line << "\n";
    m_options.log->flush();
}

bool ParseWorker::start()
{
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) return false;

    // The child's end on its stdin (it reads and writes fd 0); its stdout
    // is /dev/null so nothing it prints can reach the LSP client's stream.
    // stderr stays the server's.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);

    std::string exe = m_options.exe;
    std::string flag = "--internal-parse-worker";
    char* argv[] = {exe.data(), flag.data(), nullptr};
    pid_t pid = -1;
    const int rc = ::posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        log("could not start '" + exe + "': " + std::strerror(rc));
        return false;
    }
    m_pid = pid;
    m_fd  = fds[0];
    log("started pid " + std::to_string(pid));
    return true;
}

void ParseWorker::stop()
{
    if (m_fd >= 0) ::close(m_fd);
    m_fd = -1;
    if (m_pid > 0) {
        // Idle or broken either way: there is nothing to finish.
        ::kill(m_pid, SIGKILL);
        while (::waitpid(m_pid, nullptr, 0) < 0 && errno == EINTR) {}
    }
    m_pid = -1;
}

bool ParseWorker::roundTrip(const std::string& request, std::string& response)
{
    if (m_pid < 0 && !start()) return false;
    if (writeFrame(m_fd, request) && readFrame(m_fd, response)) return true;
    log("pid " + std::to_string(m_pid) + " failed");
    stop();
    return false;
}

WalkResult ParseWorker::walk(const std::string& source, const std::vector<SourceLine>& sourceMap,
                             bool collectDocs, const std::string& initialScope)
{
    std::unique_lock lock{m_mutex};
    m_overLimit = false;
    const std::string request =
        encodeWalkRequest(WalkRequest{source, sourceMap, collectDocs, initialScope});

    std::string response;
    WalkResult  result;
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = roundTrip(request, response) || roundTrip(request, response);
    if (ok && m_options.log) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        log("pid " + std::to_string(m_pid) + " walk " + std::to_string(ms) + " ms (" +
            std::to_string(request.size() / 1024) + " KB in, " +
            std::to_string(response.size() / 1024) + " KB out)");
    }
    if (ok && !decodeWalkResult(response, result)) {
        log("pid " + std::to_string(m_pid) + " sent a malformed result");
        stop();
        ok = false;
    }
    if (!ok) {
        WalkResult failed;
        failed.parseErrors.push_back(
            {1, 0, "svlsp: the parser worker failed; this file was not parsed"});
        return failed;
    }

    if (m_options.maxRssMb > 0) {
        if (const std::size_t mb = rssMb(m_pid); mb > m_options.maxRssMb) {
            if (m_options.idleSeconds == 0) {
                log("pid " + std::to_string(m_pid) + " at " + std::to_string(mb) +
                    " MB > " + std::to_string(m_options.maxRssMb) + " MB, recycled");
                stop();
            } else {
                log("pid " + std::to_string(m_pid) + " at " + std::to_string(mb) + " MB > " +
                    std::to_string(m_options.maxRssMb) + " MB, recycled once idle " +
                    std::to_string(m_options.idleSeconds) + " s");
                m_overLimit = true;
                m_idleSince = std::chrono::steady_clock::now();
                lock.unlock();
                m_cv.notify_all();
            }
        }
    }
    return result;
}

// ---- The worker ----

namespace {

// A walk that grows the worker by this much has filled its thread's malloc
// arena with long-lived ANTLR state (DFA states, prediction contexts)
// interleaved with the holes its temporaries left; later walks on that
// arena run ~2x slower (live UVM: an edit's walk 27 s vs 12 s, plan.md
// §6.33 C1). Those go to a fresh thread, whose own arena stays compact.
constexpr std::size_t kRetireArenaGrowthMb = 256;

// Runs walks on its own thread, so their allocations come from that
// thread's arena. Detached and never destroyed: a retired one stays parked
// for the worker's life, which keeps glibc from handing its arena to the
// next thread.
class WalkThread {
public:
    WalkThread() { std::thread{&WalkThread::run, this}.detach(); }

    WalkResult walk(const WalkRequest& req)
    {
        std::unique_lock lock{m_mutex};
        m_request = &req;
        m_cv.notify_all();
        m_cv.wait(lock, [&] { return m_request == nullptr && m_done; });
        m_done = false;
        return std::move(m_result);
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    const WalkRequest*      m_request{nullptr};
    bool                    m_done{false};
    WalkResult              m_result;

    void run()
    {
        std::unique_lock lock{m_mutex};
        for (;;) {
            m_cv.wait(lock, [&] { return m_request != nullptr; });
            const WalkRequest& req = *m_request;
            lock.unlock();
            auto result =
                SvTreeWalker::walk(req.source, req.sourceMap, req.collectDocs, req.initialScope);
            lock.lock();
            m_result  = std::move(result);
            m_request = nullptr;
            m_done    = true;
            m_cv.notify_all();
        }
    }
};

} // namespace

int runParseWorker(int fd)
{
    // Don't outlive the server (its end closing is the normal way out).
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);

    auto* walker = new WalkThread; // see WalkThread
    std::string request;
    while (readFrame(fd, request)) {
        WalkRequest req;
        if (!decodeWalkRequest(request, req)) return 2;
        const std::size_t before = rssMb(::getpid());
        const auto result = walker->walk(req);
        if (!writeFrame(fd, encodeWalkResult(result))) return 1;
        if (rssMb(::getpid()) >= before + kRetireArenaGrowthMb) walker = new WalkThread;
    }
    return 0;
}
