// Full-stack proof of plan.md §6.4 (cross-file invalidation): saving a file
// force-recompiles every other file whose own compiled unit `` `include ``s
// it, so a stale diagnostic in a dependent file doesn't linger forever, but
// only editing (without saving) never triggers this — propagation is
// deliberately gated to didSave, not every debounced didChange, to avoid
// flooding the server with cascading recompiles while the user is still
// actively typing (see plan.md §6.4's own "Note, added 2026-09-12" and
// §6.18's original reasoning, which this directly builds on).
//
// Drives a real LanguageServer over a real pipe transport (same harness
// shape as test_server_debounce.cpp/test_server_diagnostics_visibility.cpp)
// since the behavior under test lives specifically in how didSave's handler
// (src/lsp/server.cpp) and ChangeDebouncer interact, not in
// CompilationController alone (already covered directly by
// tests/unit/db/test_compilation_controller.cpp's own file_includes/
// forceRecompile cases).
#include <catch2/catch_test_macros.hpp>
#include "lsp/server.h"
#include <lsp/io/stream.h>
#include <lsp/json/json.h>

#include <fstream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

namespace {

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

    lsp::json::Value recvMessage()
    {
        std::size_t contentLength = 0;
        bool haveLength = false;
        while (true) {
            std::string line = readLine(m_fromServer[0]);
            if (line.empty())
                break;
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

    // Non-blocking: true if a full message is already waiting (or arrives
    // within timeoutMs). Used to prove a *lack* of a message -- e.g. no
    // propagation from a plain didChange -- which recvMessage() alone can't
    // express.
    bool hasPendingMessage(int timeoutMs)
    {
        pollfd pfd{m_fromServer[0], POLLIN, 0};
        int rv = ::poll(&pfd, 1, timeoutMs);
        if (rv < 0)
            throw std::runtime_error("test: poll failed");
        return rv > 0 && (pfd.revents & POLLIN) != 0;
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

struct PublishedDiagnostics {
    std::string uri;
    bool        hasVersion;
    std::size_t count;
};

PublishedDiagnostics asPublishDiagnostics(const lsp::json::Value& msg)
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

    const auto* uri = pobj.find("uri");
    REQUIRE(uri != nullptr);
    REQUIRE(uri->isString());

    const auto* diags = pobj.find("diagnostics");
    REQUIRE(diags != nullptr);
    REQUIRE(diags->isArray());

    return {uri->string(), pobj.find("version") != nullptr, diags->array().size()};
}

// Blocks until a publishDiagnostics for `uri` specifically arrives, ignoring
// (but not losing track of the count of) any interleaved messages for other
// URIs -- fan-out order across multiple includers/included files is never
// guaranteed. Fails the test outright if `maxOther` unrelated messages pass
// without finding it (guards against an infinite loop on a genuine bug).
PublishedDiagnostics recvFor(TestClient& client, const std::string& uri, int maxOther = 10)
{
    for (int i = 0; i < maxOther; ++i) {
        auto pub = asPublishDiagnostics(client.recvMessage());
        if (pub.uri == uri) return pub;
    }
    FAIL("no publishDiagnostics for " << uri << " arrived within " << maxOther << " messages");
    return {};
}

} // namespace

TEST_CASE("saving an included file force-recompiles every file that includes it",
          "[lsp][server][cross-file-invalidation]")
{
    std::string incPath = "/tmp/svlsp_test_depinv_a.sv";
    { std::ofstream ofs(incPath); ofs << "module a_mod; endmodule\n"; }

    TestClient client;
    client.initialize();

    const std::string aUri = "file://" + incPath;
    const std::string bUri = "file:///depinv_b.sv";

    client.didOpen(aUri, "module a_mod; endmodule\n", 1);
    auto pubA1 = recvFor(client, aUri);
    CHECK(pubA1.count == 0);

    const std::string bText = "`include \"" + incPath + "\"\nmodule b_mod; endmodule\n";
    client.didOpen(bUri, bText, 1);
    auto pubB1 = recvFor(client, bUri);
    CHECK(pubB1.count == 0);
    recvFor(client, aUri); // A's own diagnostics, republished as an included file

    // Edit A (didChange + rewrite the real file, mirroring what an editor
    // actually does before a save -- B's own `` `include `` resolution
    // reads A from disk, never from another buffer's in-memory text) then
    // save it.
    const std::string brokenA = "module a_mod { endmodule\n";
    { std::ofstream ofs(incPath); ofs << brokenA; }
    client.didChange(aUri, brokenA, 2);
    client.didSave(aUri);

    // A's own recompile (synchronous, part of didSave itself) now shows the
    // real error, with A's real client-tracked version attached (A is open).
    auto pubA2 = recvFor(client, aUri);
    CHECK(pubA2.count > 0);
    CHECK(pubA2.hasVersion);

    // B was never itself edited or saved, but it `include`s A -- it must
    // still be force-recompiled, arriving asynchronously off
    // m_dependencyRechecker's background thread (plan.md §6.4). B's own
    // diagnostic count correctly stays 0 -- the syntax error occurs inside
    // A's own text, so it's attributed to A's file, not to every file that
    // transitively includes it (the same per-file attribution the
    // diagnostics-visibility fix already established). B's real
    // client-tracked version is still attached, proving this really is a
    // fresh compile of the live, open document, not a fallback disk read.
    auto pubB2 = recvFor(client, bUri);
    CHECK(pubB2.count == 0);
    CHECK(pubB2.hasVersion);

    // The concrete, unambiguous proof B was actually recompiled (not just
    // "a message with count 0 happened to arrive," which alone wouldn't
    // distinguish it from doing nothing): B's own recompile re-discovers A
    // as one of its own included files and republishes A's diagnostics a
    // *second* time via collectIncludedDiagnostics -- this echo carries no
    // version (unlike pubA2's real client-tracked one above), since it's
    // published on A's behalf by B's compile, not as A's own primary one.
    auto pubA3 = recvFor(client, aUri);
    CHECK(pubA3.count > 0);
    CHECK_FALSE(pubA3.hasVersion);

    client.didClose(aUri);
    client.didClose(bUri);
    client.shutdownAndExit();
    std::remove(incPath.c_str());
}

TEST_CASE("editing (without saving) an included file does not propagate to its includers",
          "[lsp][server][cross-file-invalidation]")
{
    std::string incPath = "/tmp/svlsp_test_depinv_b.sv";
    { std::ofstream ofs(incPath); ofs << "module a_mod; endmodule\n"; }

    TestClient client;
    client.initialize();

    const std::string aUri = "file://" + incPath;
    const std::string bUri = "file:///depinv_noprop_b.sv";

    client.didOpen(aUri, "module a_mod; endmodule\n", 1);
    recvFor(client, aUri);

    const std::string bText = "`include \"" + incPath + "\"\nmodule b_mod; endmodule\n";
    client.didOpen(bUri, bText, 1);
    recvFor(client, bUri);
    recvFor(client, aUri);

    // didChange only -- no didSave. Also rewrite the real file, so if
    // propagation *did* wrongly fire from didChange alone, B's own
    // recompile would actually see the new (broken) content and fail this
    // test's own assumption that nothing changes.
    const std::string brokenA = "module a_mod { endmodule\n";
    { std::ofstream ofs(incPath); ofs << brokenA; }
    client.didChange(aUri, brokenA, 2);

    // A's own debounced recompile (plan.md §6.8) eventually fires and
    // publishes A's own updated diagnostics -- that much is expected and
    // unrelated to §6.4.
    auto pubA2 = recvFor(client, aUri);
    CHECK(pubA2.count > 0);

    // But nothing further should ever arrive for B: a plain didChange must
    // never trigger cross-file propagation, only didSave does (this is the
    // whole point of gating it this way -- see this file's own header
    // comment).
    CHECK_FALSE(client.hasPendingMessage(400));

    client.didClose(aUri);
    client.didClose(bUri);
    client.shutdownAndExit();
    std::remove(incPath.c_str());
}

TEST_CASE("saving a widely-included file force-recompiles every one of its includers",
          "[lsp][server][cross-file-invalidation]")
{
    std::string incPath = "/tmp/svlsp_test_depinv_c.sv";
    { std::ofstream ofs(incPath); ofs << "module a_mod; endmodule\n"; }

    TestClient client;
    client.initialize();

    const std::string aUri = "file://" + incPath;
    const std::string b1Uri = "file:///depinv_fanout_b1.sv";
    const std::string b2Uri = "file:///depinv_fanout_b2.sv";
    const std::string b3Uri = "file:///depinv_fanout_b3.sv";

    client.didOpen(aUri, "module a_mod; endmodule\n", 1);
    recvFor(client, aUri);

    const std::string bText = "`include \"" + incPath + "\"\nmodule dummy_mod; endmodule\n";
    for (const auto& uri : {b1Uri, b2Uri, b3Uri}) {
        client.didOpen(uri, bText, 1);
        recvFor(client, uri);
        recvFor(client, aUri);
    }

    const std::string brokenA = "module a_mod { endmodule\n";
    { std::ofstream ofs(incPath); ofs << brokenA; }
    client.didChange(aUri, brokenA, 2);
    client.didSave(aUri);

    recvFor(client, aUri); // A's own synchronous recompile

    // All three includers must eventually be force-recompiled -- in *some*
    // order, not necessarily interleaved 1:1 with their own echo of A's
    // diagnostics, since all three fire sequentially off the same
    // m_dependencyRechecker worker thread. Each one's own diagnostic count
    // correctly stays 0 (same per-file attribution reasoning as the
    // two-file test above), but each also re-discovers A as one of its own
    // included files and republishes A's diagnostics again (a version-less
    // echo, distinct from A's own client-tracked publish already drained
    // above) -- the concrete proof each one really was recompiled, not just
    // quietly skipped. Drain exactly 6 messages (3 includers + 3 echoes)
    // and bucket them by uri, order-independent.
    int b1Count = 0, b2Count = 0, b3Count = 0, aEchoCount = 0;
    for (int i = 0; i < 6; ++i) {
        auto pub = asPublishDiagnostics(client.recvMessage());
        if (pub.uri == b1Uri)      { CHECK(pub.count == 0); CHECK(pub.hasVersion); ++b1Count; }
        else if (pub.uri == b2Uri) { CHECK(pub.count == 0); CHECK(pub.hasVersion); ++b2Count; }
        else if (pub.uri == b3Uri) { CHECK(pub.count == 0); CHECK(pub.hasVersion); ++b3Count; }
        else if (pub.uri == aUri)  { CHECK(pub.count > 0); CHECK_FALSE(pub.hasVersion); ++aEchoCount; }
        else FAIL("unexpected publishDiagnostics for " << pub.uri);
    }
    CHECK(b1Count == 1);
    CHECK(b2Count == 1);
    CHECK(b3Count == 1);
    CHECK(aEchoCount == 3);

    for (const auto& uri : {aUri, b1Uri, b2Uri, b3Uri})
        client.didClose(uri);
    client.shutdownAndExit();
    std::remove(incPath.c_str());
}
