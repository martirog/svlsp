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
#include <lsp/io/stream.h>
#include <lsp/json/json.h>

#include <chrono>
#include <memory>
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
    TestClient()
    {
        if (::pipe(m_toServer) != 0 || ::pipe(m_fromServer) != 0)
            throw std::runtime_error("test: pipe() failed");

        m_serverIo = std::make_unique<PipeStream>(m_toServer[0], m_fromServer[1]);
        m_server   = std::make_unique<LanguageServer>(*m_serverIo);
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
