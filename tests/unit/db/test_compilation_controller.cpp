#include <set>
#include <catch2/catch_test_macros.hpp>
#include "db/database.h"
#include "db/symbol_database.h"
#include "db/compilation_controller.h"
#include "compiler/project_config.h"
#include "lsp/symbol_resolution.h"
#include <algorithm>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Phase 5.4 — CompilationController (DB-backed incremental compilation)
//
// Observable invariants:
//   • First compile: pipeline runs, symbols + diagnostics stored in DB.
//   • Same content again: DB hash matches → pipeline skipped (symbols
//     unchanged — verified by injecting a sentinel row between calls).
//   • Changed content: hash mismatch → pipeline re-runs, DB updated.
// ---------------------------------------------------------------------------

// Helper: produce a controller backed by an in-memory database.
namespace {
struct Fixture {
    Database              db;
    SymbolDatabase        sdb;
    CompilationController ctrl;

    Fixture()
        : db(":memory:")
        , sdb(db)
        , ctrl(sdb)
    {
        db.initSchema();
        ctrl.setCalleeResolver(resolveCallees);
    }
};
}

TEST_CASE("first compile of valid source stores symbols in DB", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module top; endmodule\n");

    auto syms = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(!syms.empty());
    CHECK(syms[0].name == "top");
}

TEST_CASE("first compile stores file hash in DB", "[db][ctrl]") {
    Fixture f;
    const std::string src = "module top; endmodule\n";
    f.ctrl.compile("/a.sv", src);
    CHECK(!f.sdb.getFileHash("/a.sv").empty());
}

TEST_CASE("compile of valid source returns no errors", "[db][ctrl]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv", "module top; endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile of malformed source returns parse errors", "[db][ctrl]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv", "module bad { endmodule\n");
    CHECK(!errs.empty());
}

TEST_CASE("compile stores diagnostics in DB", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module bad { endmodule\n");
    auto diags = f.sdb.diagnosticsForFile("/a.sv");
    CHECK(!diags.empty());
}

TEST_CASE("second compile with same content skips pipeline — symbols unchanged",
          "[db][ctrl]") {
    Fixture f;
    const std::string src = "module top; endmodule\n";
    f.ctrl.compile("/a.sv", src);

    // Inject a sentinel symbol directly into the DB.
    auto fid = f.sdb.upsertFile("/a.sv", f.sdb.getFileHash("/a.sv"));
    f.db.execute("INSERT INTO symbols (file_id,kind,name,line,col) VALUES ("
                 + std::to_string(fid) + ",'Module','__sentinel__',99,0)");

    // Same content → cache hit → replaceSymbols NOT called → sentinel survives.
    f.ctrl.compile("/a.sv", src);
    auto syms = f.sdb.symbolsForFile("/a.sv");
    bool found = false;
    for (const auto& s : syms)
        if (s.name == "__sentinel__") { found = true; break; }
    CHECK(found);
}

TEST_CASE("second compile with changed content re-runs pipeline", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module old_name; endmodule\n");

    // Verify old symbol present
    auto before = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(before.size() == 1);
    CHECK(before[0].name == "old_name");

    // Change content → hash mismatch → pipeline re-runs
    f.ctrl.compile("/a.sv", "module new_name; endmodule\n");

    auto after = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(after.size() == 1);
    CHECK(after[0].name == "new_name");
}

TEST_CASE("compile returns errors from DB on cache hit", "[db][ctrl]") {
    Fixture f;
    const std::string bad = "module bad { endmodule\n";
    auto errs1 = f.ctrl.compile("/a.sv", bad);
    REQUIRE(!errs1.empty());

    // Same content → cache hit → errors come from DB
    auto errs2 = f.ctrl.compile("/a.sv", bad);
    REQUIRE(!errs2.empty());
    CHECK(errs1[0].line    == errs2[0].line);
    CHECK(errs1[0].message == errs2[0].message);
}

TEST_CASE("independent files are cached independently", "[db][ctrl]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "module a_mod; endmodule\n");
    f.ctrl.compile("/b.sv", "module b_mod; endmodule\n");

    auto a = f.sdb.symbolsForFile("/a.sv");
    auto b = f.sdb.symbolsForFile("/b.sv");
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a[0].name == "a_mod");
    CHECK(b[0].name == "b_mod");
}

TEST_CASE("symbols from included file are stored under included file path",
          "[db][ctrl]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_inc.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }

    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";
    f.ctrl.compile("/main.sv", src);

    auto incSyms = f.sdb.symbolsForFile(incPath);
    REQUIRE(incSyms.size() == 1);
    CHECK(incSyms[0].name == "from_include");

    auto mainSyms = f.sdb.symbolsForFile("/main.sv");
    REQUIRE(mainSyms.size() == 1);
    CHECK(mainSyms[0].name == "main_mod");
}

// ---------------------------------------------------------------------------
// `includedFiles` out-param — closes the LSP diagnostics-visibility gap:
// a caller (LanguageServer::compileAndPublish) needs to know *which* files
// this compile call touched so it can also publish their diagnostics,
// already computed/persisted above but previously never reported anywhere.
// ---------------------------------------------------------------------------

TEST_CASE("compile populates includedFiles with every touched include on a cache miss",
          "[db][ctrl]") {
    Fixture f;

    std::string incA = "/tmp/svlsp_test_ctrl_included_a.sv";
    std::string incB = "/tmp/svlsp_test_ctrl_included_b.sv";
    { std::ofstream ofs(incA); ofs << "module inc_a; endmodule\n"; }
    { std::ofstream ofs(incB); ofs << "module inc_b; endmodule\n"; }

    std::string src = "`include \"" + incA + "\"\n`include \"" + incB + "\"\n"
                       "module main_mod; endmodule\n";

    std::vector<std::string> included;
    f.ctrl.compile("/main.sv", src, nullptr, &included);

    CHECK(included.size() == 2);
    CHECK(std::find(included.begin(), included.end(), incA) != included.end());
    CHECK(std::find(included.begin(), included.end(), incB) != included.end());
    // The primary file itself is never listed as one of its own "included" files.
    CHECK(std::find(included.begin(), included.end(), "/main.sv") == included.end());
}

TEST_CASE("compile clears and leaves includedFiles empty when there is nothing included",
          "[db][ctrl]") {
    Fixture f;
    std::vector<std::string> included{"stale_entry_from_a_previous_call"};
    f.ctrl.compile("/a.sv", "module top; endmodule\n", nullptr, &included);
    CHECK(included.empty());
}

TEST_CASE("compile leaves includedFiles empty on a cache hit — nothing new to report",
          "[db][ctrl]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_included_cachehit.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }
    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";

    std::vector<std::string> firstPass;
    f.ctrl.compile("/main.sv", src, nullptr, &firstPass);
    REQUIRE(firstPass.size() == 1);

    // Same content again → cache hit → nothing re-parsed, so nothing new to
    // report (the included file's own diagnostics haven't changed and were
    // already reported on the pass above).
    std::vector<std::string> secondPass;
    f.ctrl.compile("/main.sv", src, nullptr, &secondPass);
    CHECK(secondPass.empty());
}

TEST_CASE("compile persists diagnostics for an included file even when it produces "
          "no records at all", "[db][ctrl]") {
    // Regression test: the loop persisting included-file data was originally
    // keyed off recsByFile's own keys, so a file whose parse produced errors
    // but literally zero symbols (unparseable from the very first token, not
    // just a bad body) was previously silently dropped -- never persisted to
    // the DB at all, not just unpublished.
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_included_norecords.sv";
    { std::ofstream ofs(incPath); ofs << "%%% totally not SystemVerilog %%%\n"; }
    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";

    std::vector<std::string> included;
    f.ctrl.compile("/main.sv", src, nullptr, &included);

    REQUIRE(f.sdb.symbolsForFile(incPath).empty());
    auto diags = f.sdb.diagnosticsForFile(incPath);
    CHECK(!diags.empty());
    CHECK(std::find(included.begin(), included.end(), incPath) != included.end());
}

// ---------------------------------------------------------------------------
// file_includes persistence / forceRecompile -- plan.md §6.4, cross-file
// invalidation
// ---------------------------------------------------------------------------

TEST_CASE("compile persists a file_includes edge for the top-level file's own include",
          "[db][ctrl][includes]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_fileincl_a.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }
    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";

    f.ctrl.compile("/main.sv", src);

    auto includers = f.sdb.includersOf(incPath);
    REQUIRE(includers.size() == 1);
    CHECK(includers[0] == "/main.sv");
}

TEST_CASE("compile persists file_includes independent of whether includedFiles is requested",
          "[db][ctrl][includes]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_fileincl_b.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }
    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";

    // No includedFiles out-param passed at all.
    f.ctrl.compile("/main.sv", src);

    REQUIRE(f.sdb.includersOf(incPath).size() == 1);
    CHECK(f.sdb.includersOf(incPath)[0] == "/main.sv");
}

TEST_CASE("compile does not touch file_includes on a cache hit",
          "[db][ctrl][includes]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_fileincl_c.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }
    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";

    f.ctrl.compile("/main.sv", src);
    REQUIRE(f.sdb.includersOf(incPath).size() == 1);

    // Same content again -> cache hit -> replaceFileIncludes NOT called --
    // the edge from the real compile above must simply survive untouched.
    f.ctrl.compile("/main.sv", src);
    REQUIRE(f.sdb.includersOf(incPath).size() == 1);
    CHECK(f.sdb.includersOf(incPath)[0] == "/main.sv");
}

TEST_CASE("forceRecompile re-runs the pipeline even though the file's own text is unchanged",
          "[db][ctrl][includes]") {
    Fixture f;
    const std::string src = "module top; endmodule\n";
    f.ctrl.compile("/a.sv", src);

    // Inject a sentinel symbol directly into the DB -- a real recompile
    // (forced or not) always calls replaceSymbols, which would delete it.
    auto fid = f.sdb.upsertFile("/a.sv", f.sdb.getFileHash("/a.sv"));
    f.db.execute("INSERT INTO symbols (file_id,kind,name,line,col) VALUES ("
                 + std::to_string(fid) + ",'Module','__sentinel__',99,0)");

    // Same content, but forceRecompile=true this time -- unlike the ordinary
    // cache-hit case (test_compilation_controller.cpp's own "symbols
    // unchanged" test), the sentinel must NOT survive: the whole point of
    // forceRecompile is to bypass the hash-cache shortcut unconditionally
    // (plan.md §6.4 -- this file's own text is unchanged, but something it
    // `` `include ``s might not be).
    f.ctrl.compile("/a.sv", src, nullptr, nullptr, /*forceRecompile=*/true);

    auto syms = f.sdb.symbolsForFile("/a.sv");
    bool sentinelSurvived = false;
    for (const auto& s : syms)
        if (s.name == "__sentinel__") { sentinelSurvived = true; break; }
    CHECK_FALSE(sentinelSurvived);
}

TEST_CASE("forceRecompile reflects genuinely new content the same as an ordinary compile",
          "[db][ctrl][includes]") {
    // Not just "does it bypass the cache" -- forceRecompile must still
    // correctly recompile *changed* content, not just re-run against stale
    // text.
    Fixture f;
    f.ctrl.compile("/a.sv", "module old_name; endmodule\n");
    f.ctrl.compile("/a.sv", "module new_name; endmodule\n", nullptr, nullptr,
                   /*forceRecompile=*/true);

    auto syms = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "new_name");
}

TEST_CASE("compile logs the primary file and each included file to its own log stream",
          "[db][ctrl]") {
    // Regression test for moving the "[parsed]   included: <path>" line out
    // of this class's own post-hoc recsByFile loop and into
    // SvPreprocessor::process itself (real-time, as each `include` is
    // resolved) -- CompilationController's own logStream (what --build-db's
    // progress counter and --log-files both consume) must still see both
    // lines after this relocation.
    Database db(":memory:");
    db.initSchema();
    SymbolDatabase sdb(db);
    std::ostringstream log;
    CompilationController ctrl(sdb, &log);

    std::string incPath = "/tmp/svlsp_test_ctrl_log_inc.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; endmodule\n"; }

    ctrl.compile("/main_log.sv", "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n");

    CHECK(log.str() == "[parsed] /main_log.sv\n"
                        "[parsed]   included: " + incPath + "\n");
}

TEST_CASE("compile persists an instantiation as unresolved when its type isn't declared",
          "[db][ctrl][instantiation]") {
    Fixture f;
    f.ctrl.compile("/top.sv", "module top; sub u0(); endmodule\n");

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "sub") != unresolved.end());
}

TEST_CASE("compile persists an instantiation attributed to the correct file",
          "[db][ctrl][instantiation]") {
    Fixture f;

    std::string incPath = "/tmp/svlsp_test_ctrl_inc_inst.sv";
    { std::ofstream ofs(incPath); ofs << "module from_include; sub u0(); endmodule\n"; }

    std::string src = "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n";
    f.ctrl.compile("/main.sv", src);

    auto incFid = f.sdb.upsertFile(incPath, "");
    auto stmt = f.db.prepare(
        "SELECT type_name, inst_name FROM instantiations WHERE file_id = ?");
    stmt.bind(1, incFid);
    REQUIRE(stmt.step());
    CHECK(stmt.columnText(0) == "sub");
    CHECK(stmt.columnText(1) == "u0");
}

TEST_CASE("compile resolves an instantiation once its type is declared elsewhere",
          "[db][ctrl][instantiation]") {
    Fixture f;
    f.ctrl.compile("/sub.sv", "module sub; endmodule\n");
    f.ctrl.compile("/top.sv", "module top; sub u0(); endmodule\n");

    auto unresolved = f.sdb.unresolvedInstantiatedTypeNames();
    CHECK(std::find(unresolved.begin(), unresolved.end(), "sub") == unresolved.end());
}

// ---------------------------------------------------------------------------
// Missing-required-argument diagnostic for bare function/task calls
// (plan.md §6.23)
// ---------------------------------------------------------------------------

TEST_CASE("compile flags a bare call missing a required trailing argument",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  function int my_func(int a, int b);\n"
        "    my_func = a + b;\n"
        "  endfunction\n"
        "  initial my_func(1);\n"
        "endmodule\n");

    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'b'") != std::string::npos);
    CHECK(errs[0].message.find("'my_func'") != std::string::npos);
    CHECK(errs[0].line == 5);
}

TEST_CASE("compile does not flag a call supplying every required argument",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  function int my_func(int a, int b);\n"
        "    my_func = a + b;\n"
        "  endfunction\n"
        "  initial my_func(1, 2);\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile does not flag an elided positional slot that falls back to a default",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  function int my_func(int a, int b = 2, int c = 3);\n"
        "    my_func = a + b + c;\n"
        "  endfunction\n"
        "  initial my_func(1, , 5);\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile does not flag required arguments supplied entirely by name",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  function int my_func(int a, int b, int c);\n"
        "    my_func = a + b + c;\n"
        "  endfunction\n"
        "  initial my_func(.c(3), .a(1), .b(2));\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile counts comma-shorthand parameters as separate arguments",
          "[db][ctrl][call-args][tf_shorthand]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  function int my_func(input int a, b, c = 3);\n"
        "    my_func = a + b + c;\n"
        "  endfunction\n"
        "  initial my_func(1, 2);\n"
        "  initial my_func(1);\n"
        "endmodule\n");

    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'b'") != std::string::npos);
    CHECK(errs[0].line == 6);
}

TEST_CASE("compile does not flag a call to an unresolved callee name",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  initial totally_unknown_func(1);\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile does not flag a dotted call — out of scope for this check",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  class Foo;\n"
        "    function int bar(int a, int b); bar = a + b; endfunction\n"
        "  endclass\n"
        "  Foo f_inst;\n"
        "  initial f_inst.bar(1);\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile flags a missing argument to a DPI-imported function the same as an "
          "ordinary one", "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  import \"DPI-C\" function int dpi_func(int a, int b);\n"
        "  initial dpi_func(1);\n"
        "endmodule\n");

    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'b'") != std::string::npos);
    CHECK(errs[0].message.find("'dpi_func'") != std::string::npos);
}

TEST_CASE("compile flags a missing argument against a callee declared in a separate, "
          "already-compiled file", "[db][ctrl][call-args]") {
    Fixture f;
    f.ctrl.compile("/decl.sv", "function int helper(int a, int b); helper = a + b; endfunction\n");
    auto errs = f.ctrl.compile("/main.sv",
        "module top;\n"
        "  initial helper(1);\n"
        "endmodule\n");

    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'b'") != std::string::npos);
}

TEST_CASE("compile flags a missing argument against a sibling method declared later in "
          "the same class", "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  class Foo;\n"
        "    function int caller();\n"
        "      caller = callee(1);\n"
        "    endfunction\n"
        "    function int callee(int a, int b);\n"
        "      callee = a + b;\n"
        "    endfunction\n"
        "  endclass\n"
        "endmodule\n");

    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'b'") != std::string::npos);
    CHECK(errs[0].message.find("'callee'") != std::string::npos);
}

TEST_CASE("compile persists a missing-argument diagnostic for an included file",
          "[db][ctrl][call-args]") {
    Fixture f;
    std::string incPath = "/tmp/svlsp_test_ctrl_call_args_inc.sv";
    { std::ofstream ofs(incPath);
      ofs << "function int helper(int a, int b); helper = a + b; endfunction\n"
             "module from_include;\n"
             "  initial helper(1);\n"
             "endmodule\n"; }

    f.ctrl.compile("/main.sv", "`include \"" + incPath + "\"\nmodule main_mod; endmodule\n");

    auto diags = f.sdb.diagnosticsForFile(incPath);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].message.find("'b'") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Scope/type-aware call resolution (plan.md §6.26) — regression coverage for
// the false positives §6.23's own UVM-corpus probe surfaced: a Class::-
// qualified or bare in-class call resolving against an unrelated same-named
// method elsewhere instead of the actually-intended one.
// ---------------------------------------------------------------------------

TEST_CASE("a Class::-qualified call resolves to that exact class's own method, "
          "not an unrelated same-named method elsewhere", "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  class Other;\n"
        "    static function int add(int obj, int cb, int ordering); add = obj; endfunction\n"
        "  endclass\n"
        "  class Target;\n"
        "    static function int add(int rg); add = rg; endfunction\n"
        "  endclass\n"
        "  initial Target::add(1);\n"
        "endmodule\n");
    // If this resolved against `Other::add` (3 required args) instead of
    // `Target::add` (1 required arg, supplied), it would wrongly flag 'cb'
    // and 'ordering' as missing -- the exact shape of the UVM-corpus
    // `uvm_reg_read_only_cbs::add(rg)` false positive.
    CHECK(errs.empty());
}

TEST_CASE("a Class::-qualified call still flags a genuinely missing argument "
          "against that exact class's own method", "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  class Target;\n"
        "    static function int add(int rg, int extra); add = rg; endfunction\n"
        "  endclass\n"
        "  initial Target::add(1);\n"
        "endmodule\n");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'extra'") != std::string::npos);
    CHECK(errs[0].message.find("'add'") != std::string::npos);
}

TEST_CASE("a Class::-qualified call whose scope name isn't a known class is silently "
          "skipped, not flagged against an unrelated same-named method",
          "[db][ctrl][call-args]") {
    Fixture f;
    // Reproduces the UVM `type_id::get()` shape: `type_id` here is not a
    // real class declaration (just an unresolved/opaque scope name from
    // this check's point of view, the same as a typedef alias would be) --
    // `unrelated_get` stands in for a same-named `get` elsewhere that a
    // flat whole-database search would have wrongly matched pre-§6.26.
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  class SomeClass;\n"
        "    static function int get(int key); get = key; endfunction\n"
        "  endclass\n"
        "  initial type_id::get();\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("a bare in-class call resolves to a method inherited two `extends` levels "
          "up rather than an unrelated same-named method elsewhere",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "module top;\n"
        "  class Unrelated;\n"
        "    function int do_write(int t, int accessor); do_write = t; endfunction\n"
        "  endclass\n"
        "  class Grandparent;\n"
        "    virtual function int do_write(int rw); do_write = rw; endfunction\n"
        "  endclass\n"
        "  class Parent extends Grandparent;\n"
        "  endclass\n"
        "  class Child extends Parent;\n"
        "    function int caller();\n"
        "      caller = do_write(1);\n"
        "    endfunction\n"
        "  endclass\n"
        "endmodule\n");
    // If this resolved against `Unrelated::do_write` (2 required args)
    // instead of the real inherited `Grandparent::do_write` (1 required
    // arg, supplied), it would wrongly flag 'accessor' as missing -- the
    // exact shape of the UVM-corpus `uvm_reg_indirect_data::do_write(rw)`
    // false positive.
    CHECK(errs.empty());
}

TEST_CASE("compile with a config define gates an ifdef", "[db][ctrl][project-config]") {
    Fixture f;
    ProjectConfig config;
    config.defines["SIM"] = "";

    f.ctrl.compile("/a.sv", "`ifdef SIM\nmodule sim_only; endmodule\n`endif\n", &config);

    auto syms = f.sdb.symbolsForFile("/a.sv");
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "sim_only");
}

TEST_CASE("compile without a config define leaves the ifdef block inactive",
          "[db][ctrl][project-config]") {
    Fixture f;
    f.ctrl.compile("/a.sv", "`ifdef SIM\nmodule sim_only; endmodule\n`endif\n");

    auto syms = f.sdb.symbolsForFile("/a.sv");
    CHECK(syms.empty());
}

TEST_CASE("compile with a config includeDir resolves a bare `include", "[db][ctrl][project-config]") {
    Fixture f;
    std::string incPath = "/tmp/svlsp_test_ctrl_cfg_inc.sv";
    { std::ofstream ofs(incPath); ofs << "module via_config_incdir; endmodule\n"; }

    ProjectConfig config;
    config.includeDirs.push_back("/tmp");

    f.ctrl.compile("/a.sv", "`include \"svlsp_test_ctrl_cfg_inc.sv\"\n", &config);

    auto syms = f.sdb.symbolsForFile(incPath);
    REQUIRE(syms.size() == 1);
    CHECK(syms[0].name == "via_config_incdir");
}

// plan.md §6.30 step 1, end to end: the package, the `import pkg::*` and the
// importing module all live in one real compiled file.
TEST_CASE("a wildcard import of a package declared in the same file makes its members "
          "visible inside the importing module", "[db][ctrl][import]") {
    Fixture f;
    f.ctrl.compile("/a.sv",
        "package same_file_pkg;\n"          // line 1
        "  class SameFileCls;\n"
        "  endclass\n"
        "endpackage\n"
        "import same_file_pkg::*;\n"         // line 5
        "module same_file_top;\n"
        "  SameFileCls obj;\n"               // line 7
        "endmodule\n");

    auto visible = f.sdb.findSymbolsVisibleAt("/a.sv", {7, 0});
    auto n = std::count_if(visible.begin(), visible.end(),
                           [](const SymbolRow& r){ return r.name == "SameFileCls"; });
    CHECK(n == 1);
}

TEST_CASE("compile checks a bare call inside an out-of-class body against the class's extern "
          "prototype, once per parameter (plan.md §6.30 step D)",
          "[db][ctrl][call-args][outofclass]") {
    // The prototype carries the default; the body repeats the parameters
    // without it. Both sit in scope C::m, so the check must use one
    // declaration's parameters (not both), and the prototype's default
    // must count.
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "class C;\n"
        "  extern function void m(int a, int b = 1);\n"
        "  extern function void go();\n"
        "endclass\n"
        "function void C::m(int a, int b);\n"
        "endfunction\n"
        "function void C::go();\n"
        "  m(1);\n"
        "  m();\n"
        "endfunction\n");

    REQUIRE(errs.size() == 1);
    CHECK(errs[0].message.find("'a'") != std::string::npos);
    CHECK(errs[0].line == 9);
}

// ---------------------------------------------------------------------------
// Macros persisted per defining file (plan.md §6.29 part A)
// ---------------------------------------------------------------------------

TEST_CASE("compile persists macros under the file that defines them, including a "
          "macro-only include", "[db][ctrl][phase6.29]") {
    Fixture f;
    std::string incPath = "/tmp/svlsp_test_ctrl_macros_only.svh";
    { std::ofstream ofs(incPath); ofs << "`define CTRL_INC_M(ID, MSG=\"\") ID\n"; }

    std::string src = "`include \"" + incPath + "\"\n"
                      "`define CTRL_TOP_W 8\n"
                      "module ctrl_macro_m; endmodule\n";
    std::vector<std::string> included;
    f.ctrl.compile("/main.sv", src, nullptr, &included);

    auto inc = f.sdb.findMacros("CTRL_INC_M");
    REQUIRE(inc.size() == 1);
    CHECK(inc[0].filePath == incPath);
    CHECK(inc[0].line == 1);
    CHECK(inc[0].params == std::vector<std::string>{"ID", "MSG"});
    CHECK(inc[0].defaults[1] == std::optional<std::string>{"\"\""});

    auto top = f.sdb.findMacros("CTRL_TOP_W");
    REQUIRE(top.size() == 1);
    CHECK(top[0].filePath == "/main.sv");
    CHECK(top[0].line == 2);

    // A header holding only `define`s is still an included file.
    CHECK(std::find(included.begin(), included.end(), incPath) != included.end());

    // Recompiling with the define removed drops it.
    f.ctrl.compile("/main.sv", "`include \"" + incPath + "\"\nmodule ctrl_macro_m; endmodule\n");
    CHECK(f.sdb.findMacros("CTRL_TOP_W").empty());
    CHECK(f.sdb.findMacros("CTRL_INC_M").size() == 1);
}

// ---------------------------------------------------------------------------
// A multi-segment qualifier is resolved whole (UVM uvm_reg_predictor.svh:141).
// ---------------------------------------------------------------------------

namespace {
// The UVM factory shape: each class declares `typedef <registry> type_id;`,
// and only a component registry's create() requires `parent`.
const std::string kRegistries =
    "  class obj_registry;\n"
    "    static function int create(string name = \"\", int parent = 0); endfunction\n"
    "  endclass\n"
    "  class comp_registry;\n"
    "    static function int create(string name, int parent); endfunction\n"
    "  endclass\n"
    "  class item;\n"
    "    typedef obj_registry type_id;\n"
    "  endclass\n"
    "  class comp;\n"
    "    typedef comp_registry type_id;\n"
    "  endclass\n";
} // namespace

TEST_CASE("a type-parameter-qualified factory call is not resolved against the "
          "enclosing file's own type_id", "[db][ctrl][call-args]") {
    Fixture f;
    // BUSTYPE is a type parameter, not a class: `BUSTYPE::type_id::create`
    // must fail closed, never land on predictor's own (component) type_id.
    // As in UVM, predictor's file holds only its own (component) type_id, so
    // the same-file tie-break picks it for a bare `type_id` qualifier.
    REQUIRE(f.ctrl.compile("/lib.sv", "package p;\n" + kRegistries + "endpackage\n").empty());
    auto errs = f.ctrl.compile("/a.sv",
        "package q;\n"
        "  import p::*;\n"
        "  class predictor #(type BUSTYPE = int);\n"
        "    typedef comp_registry type_id;\n"
        "    static function void f();\n"
        "      void'(BUSTYPE::type_id::create(\"t\"));\n"
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n");
    CHECK(errs.empty());
}

TEST_CASE("Class::type_id::create resolves through that class's own type_id",
          "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "package p;\n" + kRegistries +
        "  class user;\n"
        "    static function void f();\n"
        "      void'(item::type_id::create(\"i\"));\n" // object registry: all defaulted
        "      void'(comp::type_id::create(\"c\"));\n" // component registry: parent missing
        "    endfunction\n"
        "  endclass\n"
        "endpackage\n");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].line == 17);
    CHECK(errs[0].message == "missing required argument 'parent' in call to 'create'");
}

TEST_CASE("a class-scoped typedef is followed to the class its own package declares",
          "[db][ctrl][call-args]") {
    // tq_b's typedef names a bare `Reg`: resolved from where the typedef is
    // declared (tq_b), not by name with a same-file tie-break (tq_a's, whose
    // create takes one argument and would hide the missing b_parent).
    Fixture f;
    auto errs = f.ctrl.compile("/tq.sv",
        "package tq_a;\n"
        "  class Reg;\n"
        "    static function int create(int a_only); endfunction\n"
        "  endclass\n"
        "endpackage\n"
        "package tq_b;\n"
        "  class Reg;\n"
        "    static function int create(string b_name, int b_parent); endfunction\n"
        "  endclass\n"
        "  class User;\n"
        "    typedef Reg type_id;\n"
        "  endclass\n"
        "  class Other;\n"
        "    static function void f(); void'(tq_b::User::type_id::create(\"x\")); endfunction\n"
        "  endclass\n"
        "endpackage\n");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].line == 14);
    CHECK(errs[0].message == "missing required argument 'b_parent' in call to 'create'");
}

// ---------------------------------------------------------------------------
// Callees resolved as hover/definition/signature help resolve them
// (resolveCallees -> resolveSymbolsAt), not by name with a same-file
// tie-break.
// ---------------------------------------------------------------------------

namespace {

const std::string kSameNamedFns =
    "package ci_two;\n"
    "  function int ci_f(int x, int y); endfunction\n"
    "endpackage\n"
    "package ci_one;\n"
    "  function int ci_f(int x); endfunction\n"
    "endpackage\n";

} // namespace

TEST_CASE("a bare call resolves through the caller's import: the imported function "
          "takes every argument supplied", "[db][ctrl][call-args]") {
    Fixture f;
    REQUIRE(f.ctrl.compile("/pkgs.sv", kSameNamedFns).empty());
    auto errs = f.ctrl.compile("/top.sv",
        "module top;\n"
        "  import ci_one::*;\n"
        "  int r;\n"
        "  initial r = ci_f(1);\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("a bare call resolves through the caller's import: the imported function's "
          "missing argument is flagged", "[db][ctrl][call-args]") {
    Fixture f;
    REQUIRE(f.ctrl.compile("/pkgs.sv",
        "package ci_one;\n"
        "  function int ci_g(int x); endfunction\n"
        "endpackage\n"
        "package ci_two;\n"
        "  function int ci_g(int x, int y); endfunction\n"
        "endpackage\n").empty());
    auto errs = f.ctrl.compile("/top.sv",
        "module top;\n"
        "  import ci_two::*;\n"
        "  int r;\n"
        "  initial r = ci_g(1);\n"
        "endmodule\n");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].line == 4);
    CHECK(errs[0].message == "missing required argument 'y' in call to 'ci_g'");
}

TEST_CASE("a pkg::-qualified call resolves in that package", "[db][ctrl][call-args]") {
    Fixture f;
    REQUIRE(f.ctrl.compile("/pkgs.sv", kSameNamedFns).empty());
    auto errs = f.ctrl.compile("/top.sv",
        "module top;\n"
        "  int r, s;\n"
        "  initial r = ci_one::ci_f(1);\n"
        "  initial s = ci_two::ci_f(1);\n"
        "endmodule\n");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].line == 4);
    CHECK(errs[0].message == "missing required argument 'y' in call to 'ci_f'");
}

TEST_CASE("a call expanded from a macro isn't checked: its name isn't in the source "
          "where it's reported", "[db][ctrl][call-args]") {
    Fixture f;
    auto errs = f.ctrl.compile("/a.sv",
        "`define CALL_IT ci_h(1)\n"
        "module top;\n"
        "  function int ci_h(int a, int b); endfunction\n"
        "  int r;\n"
        "  initial r = `CALL_IT;\n"
        "endmodule\n");
    CHECK(errs.empty());
}

TEST_CASE("compile skips the missing-argument check with no callee resolver set",
          "[db][ctrl][call-args]") {
    Database db(":memory:");
    db.initSchema();
    SymbolDatabase sdb(db);
    CompilationController ctrl(sdb);
    auto errs = ctrl.compile("/a.sv",
        "module top;\n"
        "  function int ci_k(int a, int b); endfunction\n"
        "  int r;\n"
        "  initial r = ci_k(1);\n"
        "endmodule\n");
    CHECK(errs.empty());
}

// ---------------------------------------------------------------------------
// Include context (schema v16 include_sites): a package spread over
// `include'd files, as uvm_pkg is. A position in an included file that none
// of its own scope rows contains is in the scope open at its `include, the
// package's members from the other included files are visible there, and
// the file compiled on its own (opened or edited) is seeded from its
// include site: that scope, its includer's macros and imports.
// ---------------------------------------------------------------------------

namespace {
struct IncludedPackage : Fixture {
    const std::string incA = "/tmp/svlsp_test_is_a.svh";
    const std::string incB = "/tmp/svlsp_test_is_b.svh";
    const std::string textA = "typedef int is_t;\nclass is_c;\nendclass\n";
    const std::string textB =
        "virtual class is_b extends is_c;\n" // line 1: the name is at column 14
        "  is_t v;\n"
        "  `IS_DECL(w)\n"                     // the includer's function-like macro
        "  is_qt q;\n"                        // from the includer's `import is_q::*`
        "endclass\n"
        "\n";
    IncludedPackage() {
        { std::ofstream ofs(incA); ofs << textA; }
        { std::ofstream ofs(incB); ofs << textB; }
        ctrl.compile("/is_top.sv",
                     "`define IS_DECL(n) int n;\n"
                     "package is_q;\n"
                     "  typedef int is_qt;\n"
                     "endpackage\n"
                     "package is_p;\n"
                     "import is_q::*;\n"
                     "`include \"" + incA + "\"\n" // line 7
                     "`include \"" + incB + "\"\n" // line 8
                     "endpackage\n");
    }
    std::optional<SymbolRow> row(const std::string& name) const {
        auto rows = sdb.findSymbolsByName(name);
        if (rows.empty()) return std::nullopt;
        return rows.front();
    }
};
} // namespace

TEST_CASE("a compile records where it included each file",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    const auto a = f.sdb.includeSitesOf(f.incA);
    REQUIRE(a.size() == 1);
    CHECK(a[0].primaryPath == "/is_top.sv");
    CHECK(a[0].includerPath == "/is_top.sv");
    CHECK(a[0].line == 7);
    CHECK(a[0].scope == "is_p");
    const auto b = f.sdb.includeContextOf(f.incB);
    REQUIRE(b);
    CHECK(b->line == 8);
    CHECK(b->scope == "is_p");
    CHECK_FALSE(f.sdb.includeContextOf("/is_top.sv"));
}

TEST_CASE("outside its own scopes an included file's position is in its include scope",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    CHECK(f.sdb.scopeAtPosition(f.incB, {1, 0}) == "is_p");  // before the class name
    CHECK(f.sdb.scopeAtPosition(f.incB, {6, 0}) == "is_p");  // after endclass
    CHECK(f.sdb.scopeAtPosition(f.incB, {2, 2}) == "is_p::is_b");
    CHECK(f.sdb.scopeKindAtPosition(f.incB, {1, 0}) == "Package");
    CHECK(f.sdb.scopeKindAtPosition(f.incB, {2, 2}) == "Class");
}

TEST_CASE("a package's members declared in another included file are visible",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    for (SrcPos at : {SrcPos{1, 0}, SrcPos{2, 2}, SrcPos{6, 0}}) {
        const auto rows = f.sdb.findSymbolsVisibleAt(f.incB, at);
        auto has = [&](const std::string& name) {
            return std::count_if(rows.begin(), rows.end(), [&](const SymbolRow& r) {
                return r.name == name && r.filePath == f.incA;
            });
        };
        CHECK(has("is_c") == 1);
        CHECK(has("is_t") == 1);
    }
}

TEST_CASE("a name declared in another included file of the package resolves exactly",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    auto cls = resolveSymbolAt(f.sdb, f.incB, f.textB, 0, 29); // `is_c` after extends
    REQUIRE(cls);
    CHECK(cls->exact);
    CHECK(cls->row.kind == "Class");
    CHECK(cls->row.filePath == f.incA);
    auto type = resolveSymbolAt(f.sdb, f.incB, f.textB, 1, 3); // `is_t`
    REQUIRE(type);
    CHECK(type->exact);
    CHECK(type->row.kind == "Typedef");
    CHECK(type->row.filePath == f.incA);
}

TEST_CASE("an import written in the includer applies in the included file",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    const auto rows = f.sdb.findSymbolsVisibleAt(f.incB, {4, 2});
    CHECK(std::count_if(rows.begin(), rows.end(), [](const SymbolRow& r) {
        return r.name == "is_qt" && r.scope == "is_q";
    }) == 1);
    auto type = resolveSymbolAt(f.sdb, f.incB, f.textB, 3, 3); // `is_qt`
    REQUIRE(type);
    CHECK(type->exact);
    CHECK(type->row.scope == "is_q");
}

TEST_CASE("an included file compiled on its own keeps its includer's scope and macros",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    auto errs = f.ctrl.compile(f.incB, f.textB + "// edited\n");
    CHECK(errs.empty());
    const auto b = f.row("is_b");
    REQUIRE(b);
    CHECK(b->scope == "is_p");
    const auto w = f.row("w"); // declared by the includer's `IS_DECL
    REQUIRE(w);
    CHECK(w->scope == "is_p::is_b");
    CHECK(f.sdb.scopeAtPosition(f.incB, {2, 2}) == "is_p::is_b");
    // Its include site belongs to the includer's compile and survives.
    REQUIRE(f.sdb.includeContextOf(f.incB));
    CHECK(f.sdb.includeContextOf(f.incB)->scope == "is_p");
    // ... and what is visible in it is unchanged.
    auto cls = resolveSymbolAt(f.sdb, f.incB, f.textB, 0, 29);
    REQUIRE(cls);
    CHECK(cls->exact);
    CHECK(cls->row.filePath == f.incA);
}

TEST_CASE("a file compiled on its own isn't seeded with its own include guard",
          "[db][ctrl][include-scope]") {
    Fixture f;
    const std::string inc = "/tmp/svlsp_test_is_guard.svh";
    const std::string text =
        "`ifndef IS_GUARD_SVH\n"
        "`define IS_GUARD_SVH\n"
        "class is_g;\n"
        "endclass\n"
        "`endif\n";
    { std::ofstream ofs(inc); ofs << text; }
    f.ctrl.compile("/is_guard_top.sv",
                   "package is_gp;\n`include \"" + inc + "\"\nendpackage\n");
    f.ctrl.compile(inc, text + "\n");
    const auto rows = f.sdb.findSymbolsByName("is_g");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].scope == "is_gp");
}

TEST_CASE("a file included in two different scopes says which one svlsp uses",
          "[db][ctrl][include-scope]") {
    IncludedPackage f;
    f.ctrl.compile("/is_top2.sv", "package is_r;\n`include \"" + f.incA + "\"\nendpackage\n");
    CHECK(f.sdb.includeSitesOf(f.incA).size() == 2);

    const auto diags = f.sdb.diagnosticsForFile(f.incA);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].line == 1);
    CHECK(diags[0].severity == 3);
    CHECK(diags[0].message ==
          "svlsp: `include'd in 2 different scopes: 'is_p' (/is_top.sv:7), "
          "'is_r' (/is_top2.sv:2); using 'is_p'");

    // Compiled on its own, it is seeded with the first and reports the same.
    auto errs = f.ctrl.compile(f.incA, f.textA + "\n");
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].severity == 3);
    const auto c = f.row("is_c");
    REQUIRE(c);
    CHECK(c->scope == "is_p");

    // Its sites agreeing again (is_top2 no longer includes it) clears it.
    f.ctrl.compile("/is_top2.sv", "package is_r;\nendpackage\n");
    CHECK(f.sdb.diagnosticsForFile(f.incA).empty());
    CHECK(f.ctrl.compile(f.incA, f.textA + "\n\n").empty());
}

TEST_CASE("a file with no tokens of its own gets the scope it is included in",
          "[db][ctrl][include-scope]") {
    // UVM's tlm1/uvm_tlm.svh shape: a wrapper of `include lines only, itself
    // included inside a package. Also a macro-only file at file level, and
    // a wrapper right after a class's endclass and right before endpackage.
    Fixture f;
    const std::string inner = "/tmp/svlsp_test_is_inner.svh";
    const std::string wrap  = "/tmp/svlsp_test_is_wrap.svh";
    const std::string wrap2 = "/tmp/svlsp_test_is_wrap2.svh";
    const std::string defs  = "/tmp/svlsp_test_is_defs.svh";
    const std::string innerText = "class is_inner;\nendclass\n";
    const std::string nested = "/tmp/svlsp_test_is_nested.svh";
    const std::string wrapText  = "// a wrapper\n`include \"" + nested + "\"\n"
                                  "`include \"" + inner + "\"\n";
    { std::ofstream ofs(nested); ofs << "`define IS_NESTED 1\n"; }
    { std::ofstream ofs(inner); ofs << innerText; }
    { std::ofstream ofs(wrap);  ofs << wrapText; }
    { std::ofstream ofs(wrap2); ofs << "`define IS_W2 1\n"; }
    { std::ofstream ofs(defs);  ofs << "`define IS_DEFS 1\n"; }
    f.ctrl.compile("/is_wrap_top.sv",
                   "`include \"" + defs + "\"\n"
                   "package is_wp;\n"
                   "`include \"" + wrap + "\"\n"
                   "class is_k;\n"
                   "endclass\n"
                   "`include \"" + wrap2 + "\"\n"
                   "endpackage\n");
    auto scopeOf = [&](const std::string& file) {
        const auto site = f.sdb.includeContextOf(file);
        return site ? site->scope : std::string("<none>");
    };
    CHECK(scopeOf(defs) == "");
    CHECK(scopeOf(wrap) == "is_wp");
    CHECK(scopeOf(inner) == "is_wp");
    CHECK(scopeOf(wrap2) == "is_wp");
    CHECK(scopeOf(nested) == "is_wp");

    // Opening the wrapper compiles it on its own, seeded with that scope,
    // so the file it includes keeps its rows scoped.
    CHECK(f.ctrl.compile(wrap, wrapText + "\n").empty());
    const auto rows = f.sdb.findSymbolsByName("is_inner");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].scope == "is_wp");
    // Its own sites, before its first token and after its last, are in the
    // seeded scope too.
    for (const auto& file : {nested, inner})
        for (const auto& site : f.sdb.includeSitesOf(file))
            CHECK(site.scope == "is_wp");
}

// ---------------------------------------------------------------------------
// Declare-before-use (plan.md §6.30, "Follow-up: forward typedefs and
// declare-before-use"): outside a class, a name is visible only after its
// declaration in the compilation unit's include order, unless an earlier
// forward typedef declares it. A forward typedef row is a fallback: it is
// dropped wherever the real declaration is visible.
// ---------------------------------------------------------------------------

namespace {
struct DeclBeforeUse : Fixture {
    const std::string incA = "/tmp/svlsp_test_dbu_a.svh";
    const std::string incB = "/tmp/svlsp_test_dbu_b.svh";
    const std::string textA =
        "class dbu_a;\n"                  // 1
        "  function void f();\n"          // 2
        "    int before_l;\n"             // 3
        "\n"                              // 4: the cursor
        "    int after_l;\n"              // 5: the name is at column 8
        "  endfunction\n"                 // 6
        "  function void g(); endfunction\n"
        "  int member_late;\n"            // 8
        "endclass\n";
    const std::string textB =
        "class dbu_late; endclass\n"
        "class dbu_b; endclass\n";
    const std::string top = "/dbu_top.sv";
    DeclBeforeUse() {
        { std::ofstream ofs(incA); ofs << textA; }
        { std::ofstream ofs(incB); ofs << textB; }
        ctrl.compile("/dbu_other.sv",
                     "package dbu_q;\n"
                     "  int q_item;\n"
                     "endpackage\n"
                     "typedef int dbu_other_t;\n");
        ctrl.compile(top,
                     "package dbu_p;\n"                  // 1
                     "  typedef class dbu_late;\n"       // 2: forward
                     "`include \"" + incA + "\"\n"       // 3
                     "  int pkg_mid;\n"                  // 4
                     "`include \"" + incB + "\"\n"       // 5
                     "  class dbu_after; endclass\n"     // 6
                     "  typedef class dbu_ghost;\n"      // 7: forward, no class
                     "endpackage\n"                      // 8
                     "module dbu_m;\n"                   // 9
                     "  import dbu_q::*;\n"              // 10
                     "  int early;\n"                    // 11
                     "  initial begin\n"                 // 12
                     "    early = later;\n"              // 13
                     "  end\n"                           // 14
                     "  int later;\n"                    // 15
                     "endmodule\n");                     // 16
    }
    static bool has(const std::vector<SymbolRow>& rows, const std::string& name,
                    const std::string& kind = "") {
        return std::any_of(rows.begin(), rows.end(), [&](const SymbolRow& r) {
            return r.name == name && (kind.empty() || r.kind == kind);
        });
    }
};
} // namespace

TEST_CASE("includeSitesOfUnit lists every file a compile included, with its site",
          "[db][ctrl][decl-before-use]") {
    DeclBeforeUse f;
    const auto sites = f.sdb.includeSitesOfUnit(f.top);
    REQUIRE(sites.size() == 2);
    CHECK(sites[0].file == f.incA);
    CHECK(sites[0].includer == f.top);
    CHECK(sites[0].line == 3);
    CHECK(sites[1].file == f.incB);
    CHECK(sites[1].line == 5);
    CHECK(f.sdb.includeSitesOfUnit("/dbu_other.sv").empty());
}

TEST_CASE("a local declared after the cursor isn't visible; one before it is",
          "[db][ctrl][decl-before-use]") {
    DeclBeforeUse f;
    const auto rows = f.sdb.findSymbolsVisibleAt(f.incA, {4, 4});
    CHECK(f.has(rows, "before_l"));
    CHECK_FALSE(f.has(rows, "after_l"));
    // Class members are order-independent.
    CHECK(f.has(rows, "member_late"));
    CHECK(f.has(rows, "g"));
    // On its own declaration a name is visible.
    CHECK(f.has(f.sdb.findSymbolsVisibleAt(f.incA, {5, 8}), "after_l"));
    CHECK_FALSE(f.has(f.sdb.findSymbolsVisibleAt(f.incA, {5, 7}), "after_l"));
}

TEST_CASE("the visible set filtered by name is the unfiltered set's rows of that name",
          "[db][ctrl][decl-before-use]") {
    // findSymbolsVisibleAt(path, pos, name) only fetches rows of `name`
    // (plus the chain's own class names, which declare-before-use needs):
    // it must keep and drop exactly what the whole set does.
    DeclBeforeUse f;
    const std::vector<std::pair<std::string, SrcPos>> at = {
        {f.incA, {4, 4}}, {f.incA, {5, 7}}, {f.incA, {5, 8}}, {f.top, {2, 0}},
        {f.top, {8, 0}},  {f.top, {13, 12}}, {f.top, {16, 0}}};
    std::set<std::string> names{"dbu_ghost", "no_such_name"};
    for (const auto& [path, pos] : at)
        for (const auto& r : f.sdb.findSymbolsVisibleAt(path, pos)) names.insert(r.name);
    for (const auto& r : f.sdb.findSymbolsVisibleAt(f.top, {16, 0})) names.insert(r.name);
    REQUIRE(names.size() > 10);
    for (const auto& [path, pos] : at) {
        const auto all = f.sdb.findSymbolsVisibleAt(path, pos);
        for (const auto& name : names) {
            std::multiset<int64_t> want, got;
            for (const auto& r : all)
                if (r.name == name) want.insert(r.id);
            for (const auto& r : f.sdb.findSymbolsVisibleAt(path, pos, name)) got.insert(r.id);
            INFO(path << " " << pos.line << ":" << pos.col << " " << name);
            CHECK(got == want);
        }
    }
}

TEST_CASE("module data declared after the cursor isn't visible",
          "[db][ctrl][decl-before-use]") {
    DeclBeforeUse f;
    const auto rows = f.sdb.findSymbolsVisibleAt(f.top, {13, 12});
    CHECK(f.has(rows, "early"));
    CHECK_FALSE(f.has(rows, "later"));
    CHECK(f.has(f.sdb.findSymbolsVisibleAt(f.top, {16, 0}), "later"));
    // Imported package members and another unit's top level are exempt.
    CHECK(f.has(rows, "q_item"));
    CHECK(f.has(rows, "dbu_other_t"));
}

TEST_CASE("a package class in a later included file is visible only after it or a forward typedef",
          "[db][ctrl][decl-before-use]") {
    DeclBeforeUse f;
    // In incA (included before incB): dbu_b and dbu_after aren't declared
    // yet; dbu_late is, by the forward typedef before the `include -- and
    // the real class replaces the forward row.
    const auto inA = f.sdb.findSymbolsVisibleAt(f.incA, {4, 4});
    CHECK_FALSE(f.has(inA, "dbu_b"));
    CHECK_FALSE(f.has(inA, "dbu_after"));
    CHECK_FALSE(f.has(inA, "pkg_mid"));
    CHECK(f.has(inA, "dbu_late", "Class"));
    CHECK_FALSE(f.has(inA, "dbu_late", "Typedef"));
    CHECK_FALSE(f.has(inA, "dbu_ghost"));

    // Before the forward typedef, nothing.
    const auto atTop = f.sdb.findSymbolsVisibleAt(f.top, {2, 0});
    CHECK_FALSE(f.has(atTop, "dbu_late"));
    CHECK_FALSE(f.has(atTop, "dbu_a"));

    // After everything: the classes, and the forward row alone where no
    // class exists.
    const auto atEnd = f.sdb.findSymbolsVisibleAt(f.top, {8, 0});
    CHECK(f.has(atEnd, "dbu_a"));
    CHECK(f.has(atEnd, "dbu_b"));
    CHECK(f.has(atEnd, "dbu_after"));
    CHECK(f.has(atEnd, "pkg_mid"));
    CHECK(f.has(atEnd, "dbu_late", "Class"));
    CHECK_FALSE(f.has(atEnd, "dbu_late", "Typedef"));
    CHECK(f.has(atEnd, "dbu_ghost", "Typedef"));

    // Between the two `includes: incA's class, not incB's.
    const auto mid = f.sdb.findSymbolsVisibleAt(f.top, {4, 2});
    CHECK(f.has(mid, "dbu_a"));
    CHECK_FALSE(f.has(mid, "dbu_b"));
}

TEST_CASE("an included file compiled on its own keeps its unit's declaration order",
          "[db][ctrl][decl-before-use]") {
    DeclBeforeUse f;
    REQUIRE(f.ctrl.compile(f.incA, f.textA + "\n").empty());
    const auto inA = f.sdb.findSymbolsVisibleAt(f.incA, {4, 4});
    CHECK(f.has(inA, "before_l"));
    CHECK_FALSE(f.has(inA, "after_l"));
    CHECK_FALSE(f.has(inA, "dbu_b"));
    CHECK(f.has(inA, "dbu_late", "Class"));
}
