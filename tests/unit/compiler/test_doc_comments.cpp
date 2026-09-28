#include <catch2/catch_test_macros.hpp>
#include "compiler/doc_comment.h"
#include "compiler/parse_record.h"
#include "compiler/sv_preprocessor.h"
#include "compiler/sv_tree_walker.h"
#include <fstream>

// Doc comments on declarations (plan.md §6.31).

namespace {

// Preprocesses then walks `src` the way CompilationController does, so the
// source map (directives, `ifdef, `include) is in effect.
std::vector<ParseRecord> recordsOf(const std::string& src) {
    SvPreprocessor pp;
    auto pre = pp.process(src, "doc.sv");
    REQUIRE(pre.errors.empty());
    auto walked = SvTreeWalker::walk(pre.source, pre.sourceMap);
    for (const auto& e : walked.parseErrors) UNSCOPED_INFO(e.line << ": " << e.message);
    REQUIRE(walked.parseErrors.empty());
    return walked.records;
}

std::string docOf(const std::vector<ParseRecord>& recs, const std::string& name) {
    for (const auto& r : recs)
        if (r.name == name) return r.doc;
    FAIL("no record named " << name);
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// cleanDocComment
// ---------------------------------------------------------------------------

TEST_CASE("cleanDocComment strips line-comment markers and dedents",
          "[compiler][doc][phase6.31]") {
    CHECK(cleanDocComment({"// Adds two numbers.\n", "//   indented\n"}) ==
          "Adds two numbers.\n  indented");
    CHECK(cleanDocComment({"/// triple slash\n"}) == "triple slash");
}

TEST_CASE("cleanDocComment strips block-comment markers and leading stars",
          "[compiler][doc][phase6.31]") {
    CHECK(cleanDocComment({"/** Summary.\n *\n * Details here.\n */"}) ==
          "Summary.\n\nDetails here.");
    CHECK(cleanDocComment({"/* one line */"}) == "one line");
}

TEST_CASE("cleanDocComment drops separator and tag lines",
          "[compiler][doc][phase6.31]") {
    CHECK(cleanDocComment({"//----------\n", "// CLASS: foo\n", "//==========\n"}) ==
          "CLASS: foo");
    CHECK(cleanDocComment({"// @uvm-ieee 1800.2-2020 auto 13.1.3.3\n"}).empty());
    CHECK(cleanDocComment({"//-------\n", "// ------\n"}).empty());
    CHECK(cleanDocComment({"// Real doc.\n", "// @uvm-ieee 1800.2-2020 auto 5.3.4\n"}) ==
          "Real doc.");
}

TEST_CASE("cleanDocComment trims leading and trailing empty lines",
          "[compiler][doc][phase6.31]") {
    CHECK(cleanDocComment({"//\n", "// text\n", "//\n"}) == "text");
    CHECK(cleanDocComment({"//\r\n", "// text\r\n"}) == "text");
}

// ---------------------------------------------------------------------------
// Walker: comment above
// ---------------------------------------------------------------------------

TEST_CASE("a comment directly above a declaration is its doc",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "// The top module.\n"
        "// Second line.\n"
        "module doc_top;\n"
        "  // Counts events.\n"
        "  int count;\n"
        "  // Adds one.\n"
        "  function void bump();\n"
        "  endfunction\n"
        "  // A task.\n"
        "  task run(); endtask\n"
        "endmodule\n");
    CHECK(docOf(recs, "doc_top") == "The top module.\nSecond line.");
    CHECK(docOf(recs, "count") == "Counts events.");
    CHECK(docOf(recs, "bump") == "Adds one.");
    CHECK(docOf(recs, "run") == "A task.");
}

TEST_CASE("a blank line between comment and declaration means no doc",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "// Detached.\n"
        "\n"
        "module doc_blank;\n"
        "endmodule\n");
    CHECK(docOf(recs, "doc_blank").empty());
}

TEST_CASE("only the contiguous block above is taken",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "// Old header.\n"
        "\n"
        "// Real doc.\n"
        "module doc_contig;\n"
        "endmodule\n");
    CHECK(docOf(recs, "doc_contig") == "Real doc.");
}

TEST_CASE("the previous declaration's trailing comment is not taken",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "module doc_trail_prev;\n"
        "  int a; // about a\n"
        "  int b;\n"
        "endmodule\n");
    CHECK(docOf(recs, "a") == "about a");
    CHECK(docOf(recs, "b").empty());
}

TEST_CASE("qualifiers before the keyword are part of the declaration",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "virtual class doc_quals;\n"
        "  // Put a policy.\n"
        "  pure virtual function void put_policy(int p);\n"
        "  // Local helper.\n"
        "  local static function int helper();\n"
        "    return 0;\n"
        "  endfunction\n"
        "  // Protected field.\n"
        "  protected rand int field;\n"
        "  // Extern task.\n"
        "  extern virtual task body();\n"
        "endclass\n");
    CHECK(docOf(recs, "put_policy") == "Put a policy.");
    CHECK(docOf(recs, "helper") == "Local helper.");
    CHECK(docOf(recs, "field") == "Protected field.");
    CHECK(docOf(recs, "body") == "Extern task.");
}

TEST_CASE("block comments and /** */ above a declaration",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "/** Documented class.\n"
        " *  More text.\n"
        " */\n"
        "class doc_block;\n"
        "  /* plain block */\n"
        "  int x;\n"
        "endclass\n");
    CHECK(docOf(recs, "doc_block") == "Documented class.\n More text.");
    CHECK(docOf(recs, "x") == "plain block");
}

TEST_CASE("a directive line between comment and declaration means no doc",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "// Above ifdef.\n"
        "`ifdef DOC_NEVER_DEFINED\n"
        "`endif\n"
        "module doc_ifdef;\n"
        "  // Above define.\n"
        "`define DOC_M 1\n"
        "  int y;\n"
        "endmodule\n");
    CHECK(docOf(recs, "doc_ifdef").empty());
    CHECK(docOf(recs, "y").empty());
}

TEST_CASE("a comment at the end of an included file is not the includer's doc",
          "[compiler][doc][phase6.31]") {
    const std::string inc = "/tmp/svlsp_test_doc_p31.svh";
    { std::ofstream f(inc); f << "// inc doc\n"; }
    auto recs = recordsOf(
        "`include \"" + inc + "\"\n"
        "module doc_after_inc;\n"
        "endmodule\n");
    CHECK(docOf(recs, "doc_after_inc").empty());
}

TEST_CASE("a declaration in an included file gets its doc",
          "[compiler][doc][phase6.31]") {
    const std::string inc = "/tmp/svlsp_test_doc_p31b.svh";
    { std::ofstream f(inc); f << "// Included class.\nclass doc_inc_cls;\nendclass\n"; }
    auto recs = recordsOf("`include \"" + inc + "\"\n");
    CHECK(docOf(recs, "doc_inc_cls") == "Included class.");
}

TEST_CASE("ports, parameters, enum literals, struct members and typedefs get docs",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "module doc_kinds #(\n"
        "  // Bus width.\n"
        "  parameter int W = 8\n"
        ") (\n"
        "  // Main clock.\n"
        "  input logic clk,\n"
        "  input logic rst // Reset, active high.\n"
        ");\n"
        "  // Colors.\n"
        "  typedef enum {\n"
        "    // The red one.\n"
        "    RED,\n"
        "    GREEN, // The green one.\n"
        "    BLUE\n"
        "  } color_t;\n"
        "  typedef struct {\n"
        "    // Header word.\n"
        "    int hdr;\n"
        "    int len; // Payload length.\n"
        "  } pkt_t;\n"
        "  // Loop variable.\n"
        "  genvar gi;\n"
        "endmodule\n");
    CHECK(docOf(recs, "W") == "Bus width.");
    CHECK(docOf(recs, "clk") == "Main clock.");
    CHECK(docOf(recs, "rst") == "Reset, active high.");
    CHECK(docOf(recs, "color_t") == "Colors.");
    CHECK(docOf(recs, "RED") == "The red one.");
    CHECK(docOf(recs, "GREEN") == "The green one.");
    CHECK(docOf(recs, "BLUE").empty());
    CHECK(docOf(recs, "hdr") == "Header word.");
    CHECK(docOf(recs, "len") == "Payload length.");
    CHECK(docOf(recs, "gi") == "Loop variable.");
}

TEST_CASE("function arguments get docs, and never the function's own",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "// Adds.\n"
        "function int doc_add(int a, int b);\n"
        "  return a + b;\n"
        "endfunction\n"
        "function int doc_add2(\n"
        "  // First.\n"
        "  int p,\n"
        "  int q // Second.\n"
        ");\n"
        "  return p + q;\n"
        "endfunction\n");
    CHECK(docOf(recs, "doc_add") == "Adds.");
    CHECK(docOf(recs, "a").empty());
    CHECK(docOf(recs, "b").empty());
    CHECK(docOf(recs, "p") == "First.");
    CHECK(docOf(recs, "q") == "Second.");
}

TEST_CASE("a trailing comment is the fallback only when nothing is above",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "class doc_fallback;\n"
        "  // Above wins.\n"
        "  int a; // trailing loses\n"
        "  int b; // trailing only\n"
        "  extern function void f(); // proto doc\n"
        "endclass\n");
    CHECK(docOf(recs, "a") == "Above wins.");
    CHECK(docOf(recs, "b") == "trailing only");
    CHECK(docOf(recs, "f") == "proto doc");
}

TEST_CASE("a function body's closing line comment is not its doc",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "function void doc_body();\n"
        "endfunction // doc_body\n");
    CHECK(docOf(recs, "doc_body").empty());
}

TEST_CASE("tag-only and separator-only blocks are no doc",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "class doc_tags;\n"
        "  // Real doc one blank line up.\n"
        "\n"
        "  // @uvm-ieee 1800.2-2020 auto 5.3.4\n"
        "  function void tag_only();\n"
        "  endfunction\n"
        "  //----------------------\n"
        "  function void separated();\n"
        "  endfunction\n"
        "  // Doc.\n"
        "  // @uvm-ieee 1800.2-2020 auto 5.3.5\n"
        "  function void both();\n"
        "  endfunction\n"
        "endclass\n");
    CHECK(docOf(recs, "tag_only").empty());
    CHECK(docOf(recs, "separated").empty());
    CHECK(docOf(recs, "both") == "Doc.");
}

TEST_CASE("an out-of-class body and a constructor get docs",
          "[compiler][doc][phase6.31]") {
    auto recs = recordsOf(
        "class doc_ooc;\n"
        "  // Makes one.\n"
        "  function new();\n"
        "  endfunction\n"
        "  extern function void m();\n"
        "endclass\n"
        "// Body doc.\n"
        "function void doc_ooc::m();\n"
        "endfunction\n");
    CHECK(docOf(recs, "new") == "Makes one.");
    const ParseRecord* body = nullptr;
    for (const auto& r : recs)
        if (r.name == "m") body = &r; // the last one is the body
    REQUIRE(body != nullptr);
    CHECK(body->line == 8);
    CHECK(body->doc == "Body doc.");
}

// ---------------------------------------------------------------------------
// Preprocessor: `define docs
// ---------------------------------------------------------------------------

TEST_CASE("a `define gets the comment block above it",
          "[compiler][doc][preprocessor][phase6.31]") {
    SvPreprocessor pp;
    auto result = pp.process(
        "// Max width.\n"
        "// In bits.\n"
        "`define DOC_W 32\n"
        "\n"
        "// Detached.\n"
        "\n"
        "`define DOC_NONE 1\n"
        "`define DOC_TRAIL 2 // Trailing doc.\n"
        "/* Block doc. */\n"
        "`define DOC_F(a) (a)\n"
        "// @uvm-ieee 1800.2-2020 auto B.1\n"
        "`define DOC_TAG 3\n",
        "f.sv");
    REQUIRE(result.errors.empty());
    REQUIRE(result.macros.size() == 5);
    CHECK(result.macros[0].doc == "Max width.\nIn bits.");
    CHECK(result.macros[1].doc.empty());
    CHECK(result.macros[2].doc == "Trailing doc.");
    CHECK(result.macros[2].body == "2");
    CHECK(result.macros[3].doc == "Block doc.");
    CHECK(result.macros[4].doc.empty());
}
