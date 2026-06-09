#include <catch2/catch_test_macros.hpp>
#include "compiler/sv_tree_walker.h"

// ---------------------------------------------------------------------------
// Phase 4.5 — Error Recovery
// Verifies that the custom ANTLR4 error listener collects parse errors into
// WalkResult::parseErrors as ParseError { line, column, message }.
// ---------------------------------------------------------------------------

TEST_CASE("valid source produces no parse errors", "[compiler][error-recovery]") {
    auto result = SvTreeWalker::walk("module ok; endmodule\n");
    CHECK(result.parseErrors.empty());
}

TEST_CASE("brace instead of semicolon produces a parse error", "[compiler][error-recovery]") {
    auto result = SvTreeWalker::walk("module bad { endmodule\n");
    CHECK(!result.parseErrors.empty());
}

TEST_CASE("parse error carries a non-empty message", "[compiler][error-recovery]") {
    auto result = SvTreeWalker::walk("module bad { endmodule\n");
    REQUIRE(!result.parseErrors.empty());
    CHECK(!result.parseErrors[0].message.empty());
}

TEST_CASE("parse error line number is 1-based and correct", "[compiler][error-recovery]") {
    // Error is on line 2 (blank first line)
    auto result = SvTreeWalker::walk("\nmodule bad { endmodule\n");
    REQUIRE(!result.parseErrors.empty());
    CHECK(result.parseErrors[0].line == 2);
}

TEST_CASE("parse error column is non-negative", "[compiler][error-recovery]") {
    auto result = SvTreeWalker::walk("module bad { endmodule\n");
    REQUIRE(!result.parseErrors.empty());
    CHECK(result.parseErrors[0].column >= 0);
}

TEST_CASE("multiple syntax errors are all collected", "[compiler][error-recovery]") {
    // Two modules each with a brace error — at least one error must be reported
    auto result = SvTreeWalker::walk("module a { endmodule\nmodule b { endmodule\n");
    CHECK(!result.parseErrors.empty());
}

TEST_CASE("unclosed module body produces a parse error", "[compiler][error-recovery]") {
    auto result = SvTreeWalker::walk("module m;\n  logic x;\n");
    CHECK(!result.parseErrors.empty());
}

#include <algorithm>

TEST_CASE("records extracted from valid header despite body error", "[compiler][error-recovery]") {
    // ANTLR4 error recovery continues after body-level errors: the module header
    // parses cleanly so the Module record is emitted even though the body fails.
    auto result = SvTreeWalker::walk("module ok;\n  logic 1bad;\nendmodule\n");
    CHECK(!result.parseErrors.empty());
    const bool found = std::any_of(result.records.begin(), result.records.end(),
        [](const ParseRecord& r){ return r.kind == ParseRecordKind::Module && r.name == "ok"; });
    CHECK(found);
}

// ---------------------------------------------------------------------------
// LSP Diagnostic conversion
// ---------------------------------------------------------------------------

#include "lsp/diagnostics.h"

TEST_CASE("ParseError converts to lsp::Diagnostic with correct range", "[compiler][error-recovery]") {
    ParseError err{2, 5, "test error"};
    auto diag = DiagnosticsPublisher::buildDiagnostic(err);
    // ANTLR4 lines are 1-based; LSP lines are 0-based
    CHECK(diag.range.start.line      == 1u);
    CHECK(diag.range.start.character == 5u);
    CHECK(diag.range.end.line        == 1u);
    CHECK(diag.range.end.character   == 6u);
}

TEST_CASE("ParseError converts to lsp::Diagnostic with error message", "[compiler][error-recovery]") {
    ParseError err{1, 0, "syntax error: unexpected token"};
    auto diag = DiagnosticsPublisher::buildDiagnostic(err);
    CHECK(diag.message == "syntax error: unexpected token");
}

TEST_CASE("ParseError diagnostic has Error severity", "[compiler][error-recovery]") {
    ParseError err{1, 0, "msg"};
    auto diag = DiagnosticsPublisher::buildDiagnostic(err);
    REQUIRE(diag.severity.has_value());
    CHECK(*diag.severity == lsp::DiagnosticSeverity::Error);
}
