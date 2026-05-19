#include <catch2/catch_test_macros.hpp>
#include "lsp/document_store.h"

static lsp::DidOpenTextDocumentParams makeOpenParams(
    std::string_view uriPath,
    std::string_view text,
    int version = 1)
{
    lsp::DidOpenTextDocumentParams p;
    p.textDocument.uri        = lsp::DocumentUri::fromPath(uriPath);
    p.textDocument.languageId = "systemverilog";
    p.textDocument.version    = version;
    p.textDocument.text       = std::string(text);
    return p;
}

static lsp::DidChangeTextDocumentParams makeChangeParams(
    std::string_view uriPath,
    std::string_view newText,
    int version)
{
    lsp::DidChangeTextDocumentParams p;
    p.textDocument.uri     = lsp::DocumentUri::fromPath(uriPath);
    p.textDocument.version = version;
    lsp::TextDocumentContentChangeEvent_Text change;
    change.text = std::string(newText);
    p.contentChanges.push_back(change);
    return p;
}

static lsp::DidCloseTextDocumentParams makeCloseParams(std::string_view uriPath)
{
    lsp::DidCloseTextDocumentParams p;
    p.textDocument.uri = lsp::DocumentUri::fromPath(uriPath);
    return p;
}

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_CASE("DocumentStore: initially empty", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/test.sv");
    REQUIRE_FALSE(store.contains(uri));
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

TEST_CASE("DocumentStore: open stores document", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/test.sv");
    store.open(makeOpenParams("/tmp/test.sv", "module foo; endmodule"));
    REQUIRE(store.contains(uri));
}

TEST_CASE("DocumentStore: open stores correct text and version", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/test.sv");
    store.open(makeOpenParams("/tmp/test.sv", "module foo; endmodule", 3));
    const auto& doc = store.get(uri);
    REQUIRE(doc.text == "module foo; endmodule");
    REQUIRE(doc.version == 3);
}

TEST_CASE("DocumentStore: multiple documents can be open", "[document_store]")
{
    DocumentStore store;
    auto uri1 = lsp::DocumentUri::fromPath("/tmp/a.sv");
    auto uri2 = lsp::DocumentUri::fromPath("/tmp/b.sv");
    store.open(makeOpenParams("/tmp/a.sv", "module a; endmodule"));
    store.open(makeOpenParams("/tmp/b.sv", "module b; endmodule"));
    REQUIRE(store.contains(uri1));
    REQUIRE(store.contains(uri2));
    REQUIRE(store.get(uri1).text == "module a; endmodule");
    REQUIRE(store.get(uri2).text == "module b; endmodule");
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

TEST_CASE("DocumentStore: update replaces text and version", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/test.sv");
    store.open(makeOpenParams("/tmp/test.sv", "module foo; endmodule", 1));
    store.update(makeChangeParams("/tmp/test.sv", "module bar; endmodule", 2));
    const auto& doc = store.get(uri);
    REQUIRE(doc.text == "module bar; endmodule");
    REQUIRE(doc.version == 2);
}

TEST_CASE("DocumentStore: update of unknown URI is a no-op", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/test.sv");
    REQUIRE_NOTHROW(store.update(makeChangeParams("/tmp/test.sv", "new text", 2)));
    REQUIRE_FALSE(store.contains(uri));
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------

TEST_CASE("DocumentStore: close removes document", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/test.sv");
    store.open(makeOpenParams("/tmp/test.sv", "module foo; endmodule"));
    store.close(makeCloseParams("/tmp/test.sv"));
    REQUIRE_FALSE(store.contains(uri));
}

TEST_CASE("DocumentStore: close of unknown URI is a no-op", "[document_store]")
{
    DocumentStore store;
    REQUIRE_NOTHROW(store.close(makeCloseParams("/tmp/test.sv")));
}

// ---------------------------------------------------------------------------
// get
// ---------------------------------------------------------------------------

TEST_CASE("DocumentStore: get on unknown URI throws out_of_range", "[document_store]")
{
    DocumentStore store;
    auto uri = lsp::DocumentUri::fromPath("/tmp/nonexistent.sv");
    REQUIRE_THROWS_AS(store.get(uri), std::out_of_range);
}
