// Full-stack proof that a debounced textDocument/didChange actually gets
// compiled and its updated diagnostics published — not just that
// ChangeDebouncer itself fires correctly in isolation (test_change_debouncer.cpp),
// but that the real LanguageServer, driven over real framed JSON-RPC bytes
// exactly like the real editor transport, ends up with the *right diagnostic
// content* after an edit. This is both the working proof that plan.md §6.8's
// wiring is correct and a regression guard: if a future change ever breaks
// the didChange -> ChangeDebouncer -> compileAndPublish -> publishDiagnostics
// path (wrong URI reconstruction, a stale read under the wrong lock, the
// debounce firing on stale text, etc.), the diagnostic content asserted here
// will be wrong, not just a timing/count mismatch.
#include <catch2/catch_test_macros.hpp>
#include "lsp/server.h"
#include "compiler/sv_tree_walker.h"
#include "db/database.h"
#include "db/db_lock.h"
#include "db/symbol_database.h"
#include "lsp/library_db_builder.h"
#include <algorithm>
#include <sstream>
#include <lsp/io/stream.h>
#include <lsp/json/json.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

// Known-good/known-bad snippets already proven (test_compilation_controller.cpp)
// to compile clean / trigger a real ANTLR parse error respectively.
constexpr const char* kValidSv = "module m; endmodule\n";
constexpr const char* kBadSv   = "module m { endmodule\n";

// Wraps a pair of pipe fds as the server's stdio-equivalent transport:
// reads what the test writes, writes what the test reads. A real OS pipe
// gives real blocking read/write semantics, matching how the server is
// actually driven over stdin/stdout.
class PipeStream : public lsp::io::Stream {
public:
    PipeStream(int readFd, int writeFd) : m_readFd{readFd}, m_writeFd{writeFd} {}

    void read(char* buffer, std::size_t size) override
    {
        std::size_t total = 0;
        while (total < size) {
            ssize_t n = ::read(m_readFd, buffer + total, size - total);
            if (n <= 0)
                throw lsp::io::Error{"PipeStream: read failed or pipe closed"};
            total += static_cast<std::size_t>(n);
        }
    }

    void write(const char* buffer, std::size_t size) override
    {
        std::size_t total = 0;
        while (total < size) {
            ssize_t n = ::write(m_writeFd, buffer + total, size - total);
            if (n <= 0)
                throw lsp::io::Error{"PipeStream: write failed"};
            total += static_cast<std::size_t>(n);
        }
    }

private:
    int m_readFd;
    int m_writeFd;
};

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

void writeExact(int fd, const std::string& data)
{
    std::size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::write(fd, data.data() + total, data.size() - total);
        if (n <= 0)
            throw std::runtime_error("test: write failed");
        total += static_cast<std::size_t>(n);
    }
}

std::string readExact(int fd, std::size_t size)
{
    std::string out(size, '\0');
    std::size_t total = 0;
    while (total < size) {
        ssize_t n = ::read(fd, out.data() + total, size - total);
        if (n <= 0)
            throw std::runtime_error("test: read failed or pipe closed");
        total += static_cast<std::size_t>(n);
    }
    return out;
}

std::string readLine(int fd)
{
    std::string line;
    char c;
    while (true) {
        ssize_t n = ::read(fd, &c, 1);
        if (n <= 0)
            throw std::runtime_error("test: readLine failed or pipe closed");
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }
        line += c;
    }
}

// A minimal test-side JSON-RPC client driving a real LanguageServer over a
// pair of OS pipes with real Content-Length-framed messages — the same wire
// format lsp-mode/Emacs (and every real editor) uses.
class TestClient {
public:
    // `walker`, when set, replaces the server's parse (LanguageServer::setWalker).
    explicit TestClient(CompilationController::Walker walker = {}, std::ostream* log = nullptr,
                        const std::string& version = "")
    {
        if (::pipe(m_toServer) != 0 || ::pipe(m_fromServer) != 0)
            throw std::runtime_error("test: pipe() failed");

        m_serverIo = std::make_unique<PipeStream>(m_toServer[0], m_fromServer[1]);
        m_server   = std::make_unique<LanguageServer>(*m_serverIo, log);
        if (walker) m_server->setWalker(std::move(walker));
        if (!version.empty()) m_server->setVersion(version);
        m_serverThread = std::thread{[this] { m_server->run(); }};
    }

    ~TestClient()
    {
        // Best-effort: if a test didn't reach shutdown/exit (e.g. it failed
        // an assertion first), close the write end so the server's blocking
        // read fails and run() returns, then join before destroying anything
        // the server thread might still touch.
        ::close(m_toServer[1]);
        if (m_serverThread.joinable())
            m_serverThread.join();
        ::close(m_toServer[0]);
        ::close(m_fromServer[0]);
        ::close(m_fromServer[1]);
    }

    TestClient(const TestClient&)            = delete;
    TestClient& operator=(const TestClient&) = delete;

    void sendRaw(const std::string& body)
    {
        std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
        writeExact(m_toServer[1], header);
        writeExact(m_toServer[1], body);
    }

    // Blocks until the next full framed message arrives, returns its parsed
    // JSON body. Real pipe read — naturally waits for the debounced compile
    // to actually fire, no polling/sleeping needed.
    lsp::json::Value recvMessage()
    {
        std::size_t contentLength = 0;
        bool haveLength = false;
        while (true) {
            std::string line = readLine(m_fromServer[0]);
            if (line.empty())
                break; // blank line ends the header block
            const std::string key = "Content-Length:";
            if (line.compare(0, key.size(), key) == 0) {
                haveLength = true;
                contentLength = static_cast<std::size_t>(
                    std::stoul(line.substr(key.size())));
            }
        }
        if (!haveLength)
            throw std::runtime_error("test: message with no Content-Length header");
        return lsp::json::parse(readExact(m_fromServer[0], contentLength));
    }

    void initialize()
    {
        sendRaw(R"({"jsonrpc":"2.0","id":1,"method":"initialize",)"
                R"("params":{"processId":null,"rootUri":null,"capabilities":{}}})");
        auto response = recvMessage();
        REQUIRE(response.isObject());
        REQUIRE(response.object().find("result") != nullptr);

        sendRaw(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
    }

    // initialize() for a client that supports work-done progress, with
    // `svlspOptions` (a JSON object's members) as initializationOptions.svlsp.
    void initializeWithProgress(const std::string& svlspOptions = "")
    {
        sendRaw(R"({"jsonrpc":"2.0","id":1,"method":"initialize",)"
                R"("params":{"processId":null,"rootUri":null,)"
                R"("initializationOptions":{"svlsp":{)" + svlspOptions + "}},"
                R"("capabilities":{"window":{"workDoneProgress":true}}}})");
        auto response = recvMessage();
        REQUIRE(response.isObject());
        REQUIRE(response.object().find("result") != nullptr);
        sendRaw(R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
    }

    void didChangeWatchedFile(const std::string& uri)
    {
        sendRaw(R"({"jsonrpc":"2.0","method":"workspace/didChangeWatchedFiles",)"
                R"("params":{"changes":[{"uri":")" + uri + R"(","type":2}]}})");
    }

    void executeCommand(int id, const std::string& command)
    {
        sendRaw(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
                R"(,"method":"workspace/executeCommand","params":{"command":")" + command +
                R"("}})");
    }

    void hover(int id, const std::string& uri, int line, int character)
    {
        sendRaw(R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
                R"(,"method":"textDocument/hover","params":{"textDocument":{"uri":")" + uri +
                R"("},"position":{"line":)" + std::to_string(line) + R"(,"character":)" +
                std::to_string(character) + "}}}");
    }

    // The next message that isn't about progress: answers
    // window/workDoneProgress/create and records each $/progress kind.
    lsp::json::Value recvSkippingProgress(bool* timedOut = nullptr, int timeoutMs = -1)
    {
        while (true) {
            if (timedOut && !hasPendingMessage(timeoutMs)) {
                *timedOut = true;
                return {};
            }
            auto msg = recvMessage();
            const auto* method = msg.object().find("method");
            if (method && method->string() == "window/workDoneProgress/create") {
                const auto& id = msg.object().get("id");
                sendRaw(R"({"jsonrpc":"2.0","id":)" +
                        (id.isString() ? "\"" + id.string() + "\"" : lsp::json::stringify(id)) +
                        R"(,"result":null})");
                continue;
            }
            if (method && method->string() == "$/progress") {
                progressKinds.push_back(
                    msg.object().get("params").object().get("value").object().get("kind").string());
                continue;
            }
            return msg;
        }
    }
    std::vector<std::string> progressKinds;

    // Reads messages until a progress has ended; the others are returned.
    std::vector<lsp::json::Value> recvUntilProgressEnd()
    {
        std::vector<lsp::json::Value> others;
        const auto ends = std::count(progressKinds.begin(), progressKinds.end(), "end");
        while (std::count(progressKinds.begin(), progressKinds.end(), "end") == ends) {
            if (!hasPendingMessage(20000)) throw std::runtime_error("test: no progress end");
            auto msg = recvMessage();
            const auto* method = msg.object().find("method");
            if (method && method->string() == "window/workDoneProgress/create") {
                const auto& id = msg.object().get("id");
                sendRaw(R"({"jsonrpc":"2.0","id":)" +
                        (id.isString() ? "\"" + id.string() + "\"" : lsp::json::stringify(id)) +
                        R"(,"result":null})");
            } else if (method && method->string() == "$/progress") {
                progressKinds.push_back(
                    msg.object().get("params").object().get("value").object().get("kind").string());
            } else {
                others.push_back(std::move(msg));
            }
        }
        return others;
    }

    void didOpen(const std::string& uri, const std::string& text, int version)
    {
        sendRaw(
            R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{)"
            R"("textDocument":{"uri":")" + uri + R"(","languageId":"systemverilog",)"
            R"("version":)" + std::to_string(version) + R"(,"text":")" + jsonEscape(text) +
            R"("}}})");
    }

    void didChange(const std::string& uri, const std::string& text, int version)
    {
        sendRaw(
            R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{)"
            R"("textDocument":{"uri":")" + uri + R"(","version":)" +
            std::to_string(version) +
            R"(},"contentChanges":[{"text":")" + jsonEscape(text) + R"("}]}})");
    }

    void didSave(const std::string& uri)
    {
        sendRaw(
            R"({"jsonrpc":"2.0","method":"textDocument/didSave","params":{)"
            R"("textDocument":{"uri":")" + uri + R"("}}})");
    }

    void didClose(const std::string& uri)
    {
        sendRaw(
            R"({"jsonrpc":"2.0","method":"textDocument/didClose","params":{)"
            R"("textDocument":{"uri":")" + uri + R"("}}})");
    }

    void documentSymbol(int id, const std::string& uri)
    {
        sendRaw(
            R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
            R"(,"method":"textDocument/documentSymbol","params":{)"
            R"("textDocument":{"uri":")" + uri + R"("}}})");
    }

    // Non-blocking check: true if a full message is already waiting (or
    // arrives within timeoutMs), without consuming/blocking indefinitely
    // the way recvMessage() does. Used to prove a *lack* of a message —
    // e.g. a cancelled debounce timer never firing a stale, superseded
    // publish — which recvMessage() alone can't express.
    bool hasPendingMessage(int timeoutMs)
    {
        pollfd pfd{m_fromServer[0], POLLIN, 0};
        int rv = ::poll(&pfd, 1, timeoutMs);
        if (rv < 0)
            throw std::runtime_error("test: poll failed");
        return rv > 0 && (pfd.revents & POLLIN) != 0;
    }

    void shutdownAndExit()
    {
        sendRaw(R"({"jsonrpc":"2.0","id":99,"method":"shutdown","params":null})");
        auto response = recvMessage();
        REQUIRE(response.isObject());
        REQUIRE(response.object().find("result") != nullptr);

        sendRaw(R"({"jsonrpc":"2.0","method":"exit"})");
    }

private:
    int m_toServer[2]{};
    int m_fromServer[2]{};
    std::unique_ptr<PipeStream>     m_serverIo;
    std::unique_ptr<LanguageServer> m_server;
    std::thread                     m_serverThread;
};

// Extracts (version, diagnosticsCount) from a parsed publishDiagnostics
// notification. Fails the test outright if the message isn't actually one.
std::pair<int, std::size_t> asPublishDiagnostics(const lsp::json::Value& msg)
{
    REQUIRE(msg.isObject());
    const auto& obj = msg.object();
    const auto* method = obj.find("method");
    REQUIRE(method != nullptr);
    REQUIRE(method->isString());
    REQUIRE(method->string() == "textDocument/publishDiagnostics");

    const auto* params = obj.find("params");
    REQUIRE(params != nullptr);
    REQUIRE(params->isObject());
    const auto& pobj = params->object();

    const auto* version = pobj.find("version");
    REQUIRE(version != nullptr);
    REQUIRE(version->isNumber());

    const auto* diags = pobj.find("diagnostics");
    REQUIRE(diags != nullptr);
    REQUIRE(diags->isArray());

    return {static_cast<int>(version->number()), diags->array().size()};
}

// Holds the next parse once armed, until released: a compile that is
// still parsing, for as long as a test needs.
class ParseGate {
public:
    void arm() { std::lock_guard l{m_mutex}; m_armed = true; }

    // Blocks the armed parse; every other parse goes straight through.
    void pass()
    {
        std::unique_lock l{m_mutex};
        if (!m_armed) return;
        m_armed   = false;
        m_entered = true;
        m_cv.notify_all();
        m_cv.wait(l, [this] { return m_released; });
    }

    bool waitEntered(std::chrono::milliseconds timeout)
    {
        std::unique_lock l{m_mutex};
        return m_cv.wait_for(l, timeout, [this] { return m_entered; });
    }

    void release() { std::lock_guard l{m_mutex}; m_released = true; m_cv.notify_all(); }

    CompilationController::Walker walker()
    {
        return [this](const std::string& source, const std::vector<SourceLine>& sourceMap,
                      bool collectDocs, const std::string& initialScope) {
            pass();
            return SvTreeWalker::walk(source, sourceMap, collectDocs, initialScope);
        };
    }

private:
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    bool m_armed{false}, m_entered{false}, m_released{false};
};

// Blocks every parse whose source contains `marker` until released.
class SourceGate {
public:
    explicit SourceGate(std::string marker) : m_marker(std::move(marker)) {}
    bool waitEntered(std::chrono::milliseconds timeout)
    {
        std::unique_lock l{m_mutex};
        return m_cv.wait_for(l, timeout, [this] { return m_entered; });
    }
    void release() { std::lock_guard l{m_mutex}; m_released = true; m_cv.notify_all(); }
    CompilationController::Walker walker()
    {
        return [this](const std::string& source, const std::vector<SourceLine>& sourceMap,
                      bool collectDocs, const std::string& initialScope) {
            if (source.find(m_marker) != std::string::npos) {
                std::unique_lock l{m_mutex};
                m_entered = true;
                m_cv.notify_all();
                m_cv.wait(l, [this] { return m_released; });
            }
            return SvTreeWalker::walk(source, sourceMap, collectDocs, initialScope);
        };
    }

private:
    std::string             m_marker;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    bool m_entered{false}, m_released{false};
};

std::string publishedUri(const lsp::json::Value& msg)
{
    return msg.object().get("params").object().get("uri").string();
}

std::vector<std::string> publishedMessages(const lsp::json::Value& msg)
{
    std::vector<std::string> out;
    for (const auto& d : msg.object().get("params").object().get("diagnostics").array())
        out.push_back(d.object().get("message").string());
    return out;
}

void writeFileAt(const std::string& path, const std::string& text)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path) << text;
}

} // namespace

TEST_CASE("debounced didChange republishes updated diagnostics: error -> fixed",
          "[lsp][server][debounce]")
{
    TestClient client;
    client.initialize();

    const std::string uri = "file:///debounce_test_a.sv";

    // didOpen compiles synchronously (unchanged, undebounced behavior) —
    // the bad file's real parse error is published immediately.
    client.didOpen(uri, kBadSv, 1);
    auto [v1, count1] = asPublishDiagnostics(client.recvMessage());
    CHECK(v1 == 1);
    CHECK(count1 > 0);

    // Fix the file via didChange. This is now debounced (plan.md §6.8): the
    // handler returns immediately, and the actual recompile + publish
    // happens later, on ChangeDebouncer's own thread. recvMessage() blocks
    // on the real pipe until that happens — no sleeping/polling needed, and
    // this is a real end-to-end proof the debounced path picks up the *new*
    // text, not stale content from when didChange was received.
    const auto beforeChange = std::chrono::steady_clock::now();
    client.didChange(uri, kValidSv, 2);
    auto [v2, count2] = asPublishDiagnostics(client.recvMessage());
    const auto elapsed = std::chrono::steady_clock::now() - beforeChange;

    CHECK(v2 == 2);
    CHECK(count2 == 0); // the real diagnostic content updated: error is gone
    // Loose regression guard that this genuinely went through the debounce
    // delay rather than compiling synchronously again (would defeat the
    // point of §6.8) — well under the real ~300ms default to avoid flakiness.
    CHECK(elapsed >= 50ms);

    // Break it again, proving this isn't a one-shot fluke — live diagnostic
    // updates keep working across repeated debounced edits.
    client.didChange(uri, kBadSv, 3);
    auto [v3, count3] = asPublishDiagnostics(client.recvMessage());
    CHECK(v3 == 3);
    CHECK(count3 > 0);

    client.didClose(uri);
    client.shutdownAndExit();
}

TEST_CASE("didSave cancels a pending debounce and republishes immediately, "
          "with no stale second publish once the original deadline passes",
          "[lsp][server][debounce][save]")
{
    TestClient client;
    client.initialize();

    const std::string uri = "file:///debounce_test_save.sv";

    client.didOpen(uri, kValidSv, 1);
    auto [v1, count1] = asPublishDiagnostics(client.recvMessage());
    CHECK(v1 == 1);
    CHECK(count1 == 0);

    // Schedule a debounced didChange introducing an error, then immediately
    // (well before ChangeDebouncer's ~300ms default deadline could fire)
    // send didSave. Per plan.md §6.18, didSave must cancel the pending
    // timer and force an unconditional, synchronous recompile+publish right
    // away rather than waiting out the debounce.
    const auto beforeSave = std::chrono::steady_clock::now();
    client.didChange(uri, kBadSv, 2);
    client.didSave(uri);
    auto [v2, count2] = asPublishDiagnostics(client.recvMessage());
    const auto elapsed = std::chrono::steady_clock::now() - beforeSave;

    CHECK(v2 == 2);
    CHECK(count2 > 0); // reflects the latest (post-edit) text, not the stale valid one
    // Proves this went through didSave's immediate path, not the ~300ms
    // debounce timer.
    CHECK(elapsed < 250ms);

    // Let the *original* didChange debounce deadline pass (well past the
    // ~300ms default) and confirm no further, stale publish follows —
    // proving didSave actually cancelled the pending timer, not merely won
    // a race against it.
    CHECK_FALSE(client.hasPendingMessage(400));

    client.didClose(uri);
    client.shutdownAndExit();
}

TEST_CASE("a rapid-fire burst of didChange produces exactly one publish, "
          "reflecting the final edit", "[lsp][server][debounce]")
{
    TestClient client;
    client.initialize();

    const std::string uri = "file:///debounce_test_b.sv";

    client.didOpen(uri, kValidSv, 1);
    auto [openVersion, openCount] = asPublishDiagnostics(client.recvMessage());
    CHECK(openVersion == 1);
    CHECK(openCount == 0);

    // Burst: several rapid edits, each well inside the debounce window,
    // ending on a version with a real error. Real editors coalesce far more
    // aggressively than this (client-side idle-delay), but this proves the
    // *server* itself never compiles the intermediate versions.
    for (int i = 0; i < 5; ++i)
        client.didChange(uri, kValidSv, 2 + i);
    client.didChange(uri, kBadSv, 7);

    // Exactly one publish should arrive for the whole burst, and it must
    // reflect the *last* edit (version 7, real diagnostics), not any
    // intermediate one.
    auto [version, count] = asPublishDiagnostics(client.recvMessage());
    CHECK(version == 7);
    CHECK(count > 0);

    client.didClose(uri);
    client.shutdownAndExit();
}

TEST_CASE("didChange and requests are handled while a compile is parsing",
          "[lsp][server][debounce]")
{
    // A compile holds the data mutex only to read and to write the DB, not
    // across the parse: a didChange during it must not stall the message
    // loop, and a request is answered from the previous compile's state.
    ParseGate gate;
    TestClient client{gate.walker()};
    struct ReleaseOnExit { ParseGate& g; ~ReleaseOnExit() { g.release(); } } releaseOnExit{gate};
    client.initialize();

    const std::string uri = "file:///debounce_test_parsing.sv";
    client.didOpen(uri, kValidSv, 1);
    CHECK(asPublishDiagnostics(client.recvMessage()).first == 1);

    gate.arm();
    client.didChange(uri, "module m2; endmodule\n", 2);
    REQUIRE(gate.waitEntered(5s));

    client.didChange(uri, "module m3; endmodule\n", 3);
    client.documentSymbol(7, uri);
    REQUIRE(client.hasPendingMessage(3000));
    const auto response = client.recvMessage();
    REQUIRE(response.isObject());
    const auto* id = response.object().find("id");
    REQUIRE(id != nullptr);
    CHECK(id->number() == 7);
    const auto* result = response.object().find("result");
    REQUIRE(result != nullptr);
    REQUIRE(result->isArray());
    REQUIRE(result->array().size() == 1);
    CHECK(result->array()[0].object().get("name").string() == "m"); // version 1's

    gate.release();
    CHECK(asPublishDiagnostics(client.recvMessage()).first == 2);
    CHECK(asPublishDiagnostics(client.recvMessage()).first == 3);

    client.didClose(uri);
    client.shutdownAndExit();
}

TEST_CASE("a project loads in the background: the open file answers first, cross-file "
          "diagnostics wait for the load (plan.md §6.34 S4)",
          "[lsp][server][s4]")
{
    const std::string dir = "/tmp/svlsp_test_bgload";
    std::filesystem::remove_all(dir);
    writeFileAt(dir + "/pkg.sv", "package bg_p;\n  class bg_c; endclass\nendpackage\n");
    writeFileAt(dir + "/slow.sv", "module bg_slow_marker; endmodule\n");
    const std::string use = "module bg_use;\n  import bg_p::*;\n  bg_c obj;\n  bg_missing_t m;\nendmodule\n";
    writeFileAt(dir + "/use.sv", use);
    writeFileAt(dir + "/.svlsp.json",
                R"({"files": ["slow.sv", "pkg.sv", "use.sv"]})");

    SourceGate gate{"bg_slow_marker"};
    TestClient client{gate.walker()};
    struct ReleaseOnExit { SourceGate& g; ~ReleaseOnExit() { g.release(); } } releaseOnExit{gate};
    client.initializeWithProgress();

    const std::string uri = "file://" + dir + "/use.sv";
    client.didOpen(uri, use, 1);

    // use.sv first (open), pkg.sv next (declares bg_p, which use.sv names),
    // slow.sv last -- it is still parsing while the open file is answered.
    const auto first = client.recvSkippingProgress();
    REQUIRE(publishedUri(first) == uri);
    for (const auto& m : publishedMessages(first))
        CHECK(m.find("unknown type") == std::string::npos); // held back while indexing
    REQUIRE(gate.waitEntered(10s));

    client.hover(5, uri, 2, 3); // on bg_c: pkg.sv is compiled already
    lsp::json::Value response;
    for (int i = 0; i < 10; ++i) {
        response = client.recvSkippingProgress();
        if (response.object().find("id")) break;
        // Anything published meanwhile holds back the cross-file warnings.
        for (const auto& m : publishedMessages(response))
            CHECK(m.find("unknown type") == std::string::npos);
    }
    REQUIRE(response.object().find("id") != nullptr);
    CHECK(response.object().get("id").number() == 5);
    CHECK_FALSE(response.object().get("result").isNull());

    // The load ends: the held-back warning is published, and the progress
    // ends.
    gate.release();
    bool warned = false;
    for (int i = 0; i < 5 && !warned; ++i) {
        const auto msg = client.recvSkippingProgress();
        if (!msg.object().find("method") || publishedUri(msg) != uri) continue;
        for (const auto& m : publishedMessages(msg))
            if (m == "unknown type 'bg_missing_t'") warned = true;
    }
    CHECK(warned);
    for (bool done = false; !done;) client.recvSkippingProgress(&done, 300);
    REQUIRE_FALSE(client.progressKinds.empty());
    CHECK(client.progressKinds.front() == "begin");
    CHECK(client.progressKinds.back() == "end");

    client.didClose(uri);
    client.shutdownAndExit();
}

namespace {

// A project with its DB file named in its config (plan.md §6.34 S3).
struct DbProject {
    std::string dir;
    std::string uri;
    std::string db;
    explicit DbProject(const std::string& name) : dir("/tmp/svlsp_test_s3/" + name)
    {
        std::filesystem::remove_all(dir);
        writeFileAt(dir + "/a.sv", "package s3_p;\n  class s3_c; endclass\nendpackage\n");
        writeFileAt(dir + "/b.sv", use());
        writeFileAt(dir + "/.svlsp.json",
                    R"({"files": ["a.sv", "b.sv"], "database": "proj.db"})");
        uri = "file://" + dir + "/b.sv";
        db = dir + "/proj.db";
    }
    static std::string use() { return "module s3_m;\n  import s3_p::*;\n  s3_c o;\nendmodule\n"; }
    void build(const std::string& version)
    {
        REQUIRE(LibraryDbBuilder::build(dir + "/.svlsp.json", db, nullptr, version).ok);
    }
    std::string storedHash(const std::string& rel)
    {
        Database d(":memory:");
        d.reopen(db, /*readOnly=*/true);
        SymbolDatabase sdb(d);
        return sdb.getFileHash(dir + "/" + rel);
    }
};

// The files a server's log says it parsed (not cache hits).
std::vector<std::string> parsedFiles(const std::string& log)
{
    std::vector<std::string> out;
    std::istringstream in(log);
    for (std::string line; std::getline(in, line);)
        if (line.rfind("[parsed] ", 0) == 0 && line.find("(cached)") == std::string::npos &&
            line.find("included:") == std::string::npos)
            out.push_back(line.substr(9));
    return out;
}

} // namespace

TEST_CASE("a server started on a built project DB compiles nothing (plan.md §6.34 S3)",
          "[lsp][server][s3]")
{
    DbProject p("restart");
    p.build("vtest");
    std::ostringstream log;
    {
        TestClient client{{}, &log, "vtest"};
        client.initializeWithProgress();
        client.didOpen(p.uri, DbProject::use(), 1);
        client.recvUntilProgressEnd();
        client.hover(5, p.uri, 2, 3);
        auto response = client.recvSkippingProgress();
        while (!response.object().find("id")) response = client.recvSkippingProgress();
        CHECK_FALSE(response.object().get("result").isNull());
        client.shutdownAndExit();
    }
    CHECK(parsedFiles(log.str()).empty());
}

TEST_CASE("a project DB recorded by another svlsp build is reset (plan.md §6.34 S3)",
          "[lsp][server][s3]")
{
    DbProject p("version");
    p.build("vold");
    std::ostringstream log;
    {
        TestClient client{{}, &log, "vnew"};
        client.initializeWithProgress();
        client.didOpen(p.uri, DbProject::use(), 1);
        client.recvUntilProgressEnd();
        client.shutdownAndExit();
    }
    const auto parsed = parsedFiles(log.str());
    CHECK(std::count(parsed.begin(), parsed.end(), p.dir + "/a.sv") == 1);
}

TEST_CASE("while a --build-db holds the lock, a server serves the DB read-only, then takes "
          "over and refreshes (plan.md §6.34 S3)",
          "[lsp][server][s3]")
{
    DbProject p("handover");
    p.build("vtest");
    const std::string oldHash = p.storedHash("a.sv");
    writeFileAt(p.dir + "/a.sv",
                "package s3_p;\n  class s3_c; endclass\n  class s3_new; endclass\nendpackage\n");
    DbLock builder;
    REQUIRE(builder.acquire(p.db, "build-db"));

    std::ostringstream log;
    TestClient client{{}, &log, "vtest"};
    client.initializeWithProgress();
    client.didOpen(p.uri, DbProject::use(), 1);
    client.hover(5, p.uri, 2, 3); // answered from what the build committed
    auto response = client.recvSkippingProgress();
    while (!response.object().find("id")) response = client.recvSkippingProgress();
    CHECK_FALSE(response.object().get("result").isNull());
    CHECK(parsedFiles(log.str()).empty()); // nothing compiled while waiting

    builder.release();
    client.recvUntilProgressEnd();
    const auto parsed = parsedFiles(log.str());
    CHECK(std::count(parsed.begin(), parsed.end(), p.dir + "/a.sv") == 1);
    CHECK(p.storedHash("a.sv") != oldHash); // written to the file
    CHECK(DbLock::holder(p.db) == DbLock::Holder::Server);
    client.shutdownAndExit();
}

TEST_CASE("a second server on the same DB works on a copy and never writes the file "
          "(plan.md §6.34 D2)",
          "[lsp][server][s3]")
{
    DbProject p("copy");
    p.build("vtest");
    TestClient first{{}, nullptr, "vtest"};
    first.initializeWithProgress();
    first.didOpen(p.uri, DbProject::use(), 1);
    first.recvUntilProgressEnd();
    REQUIRE(DbLock::holder(p.db) == DbLock::Holder::Server);

    const std::string oldHash = p.storedHash("a.sv");
    writeFileAt(p.dir + "/a.sv",
                "package s3_p;\n  class s3_c; endclass\n  class s3_copy; endclass\nendpackage\n");
    std::ostringstream log;
    {
        TestClient second{{}, &log, "vtest"};
        second.initializeWithProgress();
        second.didOpen(p.uri, DbProject::use(), 1);
        second.recvUntilProgressEnd();
        second.hover(5, p.uri, 2, 3);
        auto response = second.recvSkippingProgress();
        while (!response.object().find("id")) response = second.recvSkippingProgress();
        CHECK_FALSE(response.object().get("result").isNull());
        second.shutdownAndExit();
    }
    const auto parsed = parsedFiles(log.str());
    CHECK(std::count(parsed.begin(), parsed.end(), p.dir + "/a.sv") == 1); // refreshed in memory
    CHECK(p.storedHash("a.sv") == oldHash);                              // the file untouched
    first.shutdownAndExit();
}

TEST_CASE("a checkout's file events refresh the project once; svlsp.refresh refreshes too "
          "(plan.md §6.34 S6)",
          "[lsp][server][s6]")
{
    const std::string dir = "/tmp/svlsp_test_s6";
    std::filesystem::remove_all(dir);
    writeFileAt(dir + "/a.sv", "package s6_p;\n  class s6_c; endclass\nendpackage\n");
    const std::string use = "module s6_m;\n  import s6_p::*;\n  s6_c o;\nendmodule\n";
    writeFileAt(dir + "/b.sv", use);
    writeFileAt(dir + "/.svlsp.json", R"({"files": ["a.sv", "b.sv"]})");
    const std::string uri = "file://" + dir + "/b.sv";

    std::ostringstream log;
    TestClient client{{}, &log};
    client.initializeWithProgress();
    client.didOpen(uri, use, 1);
    client.recvUntilProgressEnd();

    // A checkout: a.sv changes, and the client reports it many times over.
    writeFileAt(dir + "/a.sv",
                "package s6_p;\n  class s6_c; endclass\n  class s6_new; endclass\nendpackage\n");
    log.str("");
    for (int i = 0; i < 50; ++i) client.didChangeWatchedFile("file://" + dir + "/a.sv");
    client.recvUntilProgressEnd();
    CHECK(std::count(client.progressKinds.begin(), client.progressKinds.end(), "begin") == 2);
    auto parsed = parsedFiles(log.str());
    CHECK(std::count(parsed.begin(), parsed.end(), dir + "/a.sv") == 1);
    CHECK(std::count(parsed.begin(), parsed.end(), dir + "/b.sv") == 0);
    // No second refresh follows.
    CHECK_FALSE(client.hasPendingMessage(1500));

    // An event for the open document is ignored: its buffer is the truth.
    client.didChangeWatchedFile(uri);
    CHECK_FALSE(client.hasPendingMessage(1500));

    // The command refreshes too (nothing changed: nothing compiled).
    log.str("");
    client.executeCommand(7, "svlsp.refresh");
    const auto others = client.recvUntilProgressEnd();
    CHECK(std::any_of(others.begin(), others.end(), [](const lsp::json::Value& m) {
        return m.object().find("id") && m.object().get("id").number() == 7;
    }));
    CHECK(parsedFiles(log.str()).empty());
    client.shutdownAndExit();
}
