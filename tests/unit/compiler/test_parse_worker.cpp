#include <catch2/catch_test_macros.hpp>
#include "compiler/parse_worker.h"
#include "compiler/sv_tree_walker.h"
#include <csignal>
#include <sstream>

// plan.md §6.33 C1. The worker is the real svlsp binary (SVLSP_EXE, built
// before unit_tests) in its `--internal-parse-worker` mode.

namespace {

// Lines 1-6 the primary file, 7-10 "inc.svh" `include'd inside the
// package (so the walk records an include scope for it), with a column
// shift on line 3 and a syntax error on line 12.
const std::string kSource =
    "package p;\n"                                   // 1
    "  /// A doc comment.\n"                         // 2
    "  function int f(int a, int b = 2); return a; endfunction\n" // 3
    "  import q::*;\n"                               // 4
    "  class C; local int m; endclass\n"             // 5
    "  task t(); f(.a(1)); f(1, ); endtask\n"        // 6
    "  typedef struct { int x; } s_t;\n"             // 7 (inc.svh 1)
    "  int v;\n"                                     // 8 (inc.svh 2)
    "endpackage\n"                                   // 9
    "module top;\n"                                  // 10
    "  sub u_sub();\n"                               // 11
    "  int = ;\n"                                    // 12
    "endmodule\n";                                   // 13

std::vector<SourceLine> sourceMap()
{
    std::vector<SourceLine> map;
    for (int i = 1; i <= 6; ++i) map.push_back({"", i});
    map[2].colShifts = {{10, 3}, {20, -1}};
    map.push_back({"/inc.svh", 1});
    map.push_back({"/inc.svh", 2});
    for (int i = 7; i <= 11; ++i) map.push_back({"", i});
    return map;
}

// The encoding of everything but includeScopes (an unordered_map, whose
// order a decode needn't keep).
std::string withoutScopes(WalkResult r)
{
    r.includeScopes.clear();
    return encodeWalkResult(r);
}

ParseWorker::Options options(std::size_t maxRssMb = 1024)
{
    ParseWorker::Options o;
    o.exe      = SVLSP_EXE;
    o.maxRssMb = maxRssMb;
    return o;
}

} // namespace

TEST_CASE("parse worker: a request survives encode/decode", "[parse_worker]")
{
    const WalkRequest req{kSource, sourceMap(), false, "outer::inner"};
    WalkRequest back;
    REQUIRE(decodeWalkRequest(encodeWalkRequest(req), back));
    CHECK(back.source == req.source);
    CHECK(back.collectDocs == false);
    CHECK(back.initialScope == "outer::inner");
    REQUIRE(back.sourceMap.size() == req.sourceMap.size());
    CHECK(back.sourceMap[2].colShifts.size() == 2);
    CHECK(back.sourceMap[2].colShifts[1].delta == -1);
    CHECK(back.sourceMap[6].file == "/inc.svh");
}

TEST_CASE("parse worker: a walk result survives encode/decode", "[parse_worker]")
{
    const auto walked = SvTreeWalker::walk(kSource, sourceMap());
    // The fixture exercises every part of a result.
    REQUIRE_FALSE(walked.records.empty());
    REQUIRE_FALSE(walked.parseErrors.empty());
    REQUIRE_FALSE(walked.imports.empty());
    REQUIRE_FALSE(walked.instantiations.empty());
    REQUIRE_FALSE(walked.calls.empty());
    REQUIRE_FALSE(walked.includeScopes.empty());

    WalkResult back;
    REQUIRE(decodeWalkResult(encodeWalkResult(walked), back));
    CHECK(withoutScopes(back) == withoutScopes(walked));
    CHECK(back.includeScopes == walked.includeScopes);
}

TEST_CASE("parse worker: a truncated buffer does not decode", "[parse_worker]")
{
    const std::string buf = encodeWalkResult(SvTreeWalker::walk(kSource, sourceMap()));
    WalkResult out;
    CHECK_FALSE(decodeWalkResult(buf.substr(0, buf.size() - 1), out));
    CHECK_FALSE(decodeWalkResult(buf + "x", out));
    WalkRequest req;
    CHECK_FALSE(decodeWalkRequest(std::string("\xff\xff\xff\x7f", 4), req));
}

TEST_CASE("parse worker: the worker's walk is the in-process walk", "[parse_worker]")
{
    ParseWorker worker{options()};
    CHECK(worker.pid() == -1); // started on the first walk
    for (bool docs : {true, false}) {
        const auto viaWorker = worker.walk(kSource, sourceMap(), docs, "s");
        const auto inProcess = SvTreeWalker::walk(kSource, sourceMap(), docs, "s");
        CHECK(withoutScopes(viaWorker) == withoutScopes(inProcess));
        CHECK(viaWorker.includeScopes == inProcess.includeScopes);
    }
}

TEST_CASE("parse worker: one worker serves many walks", "[parse_worker]")
{
    ParseWorker worker{options()};
    worker.walk("module a; endmodule\n", {}, true, "");
    const pid_t first = worker.pid();
    REQUIRE(first > 0);
    const auto r = worker.walk("module b; endmodule\n", {}, true, "");
    CHECK(worker.pid() == first);
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].name == "b");
}

TEST_CASE("parse worker: a worker over the memory limit is recycled", "[parse_worker]")
{
    std::ostringstream log;
    auto o = options(1); // any real process is over 1 MB
    o.log  = &log;
    ParseWorker worker{o};
    const auto r = worker.walk("module a; endmodule\n", {}, true, "");
    CHECK(r.records.size() == 1);
    CHECK(worker.pid() == -1);
    CHECK(log.str().find("recycled") != std::string::npos);
    // The next walk starts a fresh one.
    CHECK(worker.walk("module b; endmodule\n", {}, true, "").records.size() == 1);
}

TEST_CASE("parse worker: a worker that died is restarted", "[parse_worker]")
{
    ParseWorker worker{options()};
    worker.walk("module a; endmodule\n", {}, true, "");
    const pid_t first = worker.pid();
    REQUIRE(first > 0);
    ::kill(first, SIGKILL);
    const auto r = worker.walk("module b; endmodule\n", {}, true, "");
    REQUIRE(r.records.size() == 1);
    CHECK(r.records[0].name == "b");
    CHECK(worker.pid() != first);
}

TEST_CASE("parse worker: no worker binary gives an error diagnostic", "[parse_worker]")
{
    auto o = options();
    o.exe  = "/nonexistent/svlsp";
    ParseWorker worker{o};
    const auto r = worker.walk("module a; endmodule\n", {}, true, "");
    CHECK(r.records.empty());
    REQUIRE(r.parseErrors.size() == 1);
    CHECK(r.parseErrors[0].line == 1);
    CHECK(r.parseErrors[0].message.find("parser worker failed") != std::string::npos);
}
