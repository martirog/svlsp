// Full-stack proof that diagnostics for a transitively `` `include ``d file
// are actually published to the client, not just computed and persisted to
// the DB — the "LSP diagnostics-visibility gap" plan.md and handoff.md both
// document: `CompilationController::compile()` already partitions and
// persists diagnostics per file, but until this fix `LanguageServer` only
// ever called publishDiagnostics for the primary (`didOpen`ed) URI, silently
// dropping every included file's own diagnostics on the floor.
//
// Drives a real LanguageServer over real framed JSON-RPC bytes (the same
// PipeStream/TestClient shape test_server_debounce.cpp already established)
// rather than calling CompilationController directly, since the bug this
// guards against lives specifically in LanguageServer::compileAndPublish's
// own wiring (which files it iterates, how it builds their URIs, what
// version it attaches) — not in the compiler pipeline underneath it, which
// tests/unit/db/test_compilation_controller.cpp already covers directly.
#include <catch2/catch_test_macros.hpp>
#include "lsp/server.h"
#include <lsp/io/stream.h>
#include <lsp/json/json.h>

#include <fstream>
#include <memory>
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

// Extracts (uri, hasVersion, diagnosticsCount) from a parsed
// publishDiagnostics notification. Fails the test outright if the message
// isn't actually one.
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

} // namespace

TEST_CASE("didOpen on a primary file publishes diagnostics for both the primary "
          "and its included file", "[lsp][server][diagnostics-visibility]")
{
    // A syntactically broken included file, `` `include ``d from a
    // syntactically clean primary — proves the included file's own error is
    // published under its own URI, distinct from (and in addition to) the
    // primary's own, empty diagnostic set.
    std::string incPath = "/tmp/svlsp_test_diagvis_inc.sv";
    { std::ofstream ofs(incPath); ofs << "module broken { endmodule\n"; }

    TestClient client;
    client.initialize();

    const std::string mainUri = "file:///diagvis_main.sv";
    const std::string mainSrc = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";
    client.didOpen(mainUri, mainSrc, 1);

    // Two publishDiagnostics notifications should arrive from this one
    // didOpen: one for the primary file (clean), one for the included file
    // (the injected syntax error) -- order isn't guaranteed, so collect both
    // by URI.
    auto first  = asPublishDiagnostics(client.recvMessage());
    auto second = asPublishDiagnostics(client.recvMessage());

    const PublishedDiagnostics* mainPub = nullptr;
    const PublishedDiagnostics* incPub  = nullptr;
    if (first.uri == mainUri)       mainPub = &first;
    else if (second.uri == mainUri) mainPub = &second;
    if (first.uri.find("diagvis_inc.sv") != std::string::npos)       incPub = &first;
    else if (second.uri.find("diagvis_inc.sv") != std::string::npos) incPub = &second;

    REQUIRE(mainPub != nullptr);
    REQUIRE(incPub  != nullptr);

    CHECK(mainPub->count == 0);
    CHECK(mainPub->hasVersion); // the client's own didOpen version (1) is echoed back

    CHECK(incPub->count > 0);
    // The included file was never didOpen'd -- no client-tracked version to
    // attach, so the LSP spec's own optional `version` field must be absent
    // entirely, not merely null/0.
    CHECK_FALSE(incPub->hasVersion);

    client.didClose(mainUri);
    client.shutdownAndExit();
    std::remove(incPath.c_str());
}
