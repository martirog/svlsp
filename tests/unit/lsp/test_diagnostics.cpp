#include <catch2/catch_test_macros.hpp>
#include "lsp/diagnostics.h"
#include <optional>

static lsp::DocumentUri makeUri(std::string_view path)
{
    return lsp::DocumentUri::fromPath(path);
}

static lsp::Diagnostic makeDiagnostic(unsigned int line, unsigned int col,
                                       const std::string& message,
                                       lsp::DiagnosticSeverity severity)
{
    lsp::Diagnostic d;
    d.range.start = {.line = line, .character = col};
    d.range.end   = {.line = line, .character = col + 1};
    d.message     = message;
    d.severity    = lsp::DiagnosticSeverityEnum{severity};
    d.source      = "svlsp";
    return d;
}

// ---------------------------------------------------------------------------
// buildParams — URI and version
// ---------------------------------------------------------------------------

TEST_CASE("DiagnosticsPublisher: buildParams sets URI", "[diagnostics]")
{
    auto uri    = makeUri("/tmp/test.sv");
    auto params = DiagnosticsPublisher::buildParams(uri, 1);
    REQUIRE(params.uri.toString() == uri.toString());
}

TEST_CASE("DiagnosticsPublisher: buildParams sets version", "[diagnostics]")
{
    auto params = DiagnosticsPublisher::buildParams(makeUri("/tmp/test.sv"), 7);
    REQUIRE(params.version.has_value());
    REQUIRE(*params.version == 7);
}

TEST_CASE("DiagnosticsPublisher: buildParams with std::nullopt leaves version unset",
          "[diagnostics]")
{
    // For a file the client never `didOpen`ed (e.g. an `` `include ``d file
    // whose diagnostics are published on the primary file's behalf) there is
    // no client-tracked version to attach -- the LSP spec's own `version`
    // field is optional for exactly this case.
    auto params = DiagnosticsPublisher::buildParams(makeUri("/tmp/test.sv"), std::nullopt);
    REQUIRE_FALSE(params.version.has_value());
}

// ---------------------------------------------------------------------------
// buildParams — empty diagnostics (Phase 3.2 default)
// ---------------------------------------------------------------------------

TEST_CASE("DiagnosticsPublisher: buildParams produces empty diagnostics by default",
          "[diagnostics]")
{
    auto params = DiagnosticsPublisher::buildParams(makeUri("/tmp/test.sv"), 1);
    REQUIRE(params.diagnostics.empty());
}

TEST_CASE("DiagnosticsPublisher: buildParams accepts explicit empty vector",
          "[diagnostics]")
{
    auto params = DiagnosticsPublisher::buildParams(makeUri("/tmp/test.sv"), 1, {});
    REQUIRE(params.diagnostics.empty());
}

// ---------------------------------------------------------------------------
// buildParams — with diagnostics (Phase 4 path, exercised here for API coverage)
// ---------------------------------------------------------------------------

TEST_CASE("DiagnosticsPublisher: buildParams includes provided diagnostics",
          "[diagnostics]")
{
    lsp::Array<lsp::Diagnostic> diags;
    diags.push_back(makeDiagnostic(3, 5, "unexpected token", lsp::DiagnosticSeverity::Error));
    diags.push_back(makeDiagnostic(7, 0, "implicit net", lsp::DiagnosticSeverity::Warning));

    auto params = DiagnosticsPublisher::buildParams(makeUri("/tmp/foo.sv"), 2,
                                                    std::move(diags));
    REQUIRE(params.diagnostics.size() == 2);
    REQUIRE(params.diagnostics[0].message == "unexpected token");
    REQUIRE(params.diagnostics[1].message == "implicit net");
}

TEST_CASE("DiagnosticsPublisher: buildParams preserves diagnostic range",
          "[diagnostics]")
{
    lsp::Array<lsp::Diagnostic> diags;
    diags.push_back(makeDiagnostic(9, 4, "error", lsp::DiagnosticSeverity::Error));

    auto params = DiagnosticsPublisher::buildParams(makeUri("/tmp/foo.sv"), 1,
                                                    std::move(diags));
    REQUIRE(params.diagnostics[0].range.start.line      == 9);
    REQUIRE(params.diagnostics[0].range.start.character == 4);
}
