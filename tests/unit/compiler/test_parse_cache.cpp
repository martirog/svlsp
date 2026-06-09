#include <catch2/catch_test_macros.hpp>
#include "compiler/parse_cache.h"

// ---------------------------------------------------------------------------
// Phase 4.6 — Incremental Parsing / ParseCache
// ---------------------------------------------------------------------------

static WalkResult makeResult(int errorCount) {
    WalkResult r;
    for (int i = 0; i < errorCount; ++i)
        r.parseErrors.push_back({i + 1, 0, "err"});
    return r;
}

TEST_CASE("isUpToDate returns false for unknown URI", "[compiler][parse-cache]") {
    ParseCache cache;
    CHECK(!cache.isUpToDate("file:///foo.sv", "content"));
}

TEST_CASE("isUpToDate returns true after storing same content", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///foo.sv", "module m; endmodule\n", makeResult(0));
    CHECK(cache.isUpToDate("file:///foo.sv", "module m; endmodule\n"));
}

TEST_CASE("isUpToDate returns false after storing different content", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///foo.sv", "module m; endmodule\n", makeResult(0));
    CHECK(!cache.isUpToDate("file:///foo.sv", "module m2; endmodule\n"));
}

TEST_CASE("get returns the stored WalkResult", "[compiler][parse-cache]") {
    ParseCache cache;
    auto result = makeResult(2);
    cache.store("file:///foo.sv", "content", result);
    const auto& got = cache.get("file:///foo.sv");
    CHECK(got.parseErrors.size() == 2);
}

TEST_CASE("store overwrites an existing entry", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///foo.sv", "v1", makeResult(1));
    cache.store("file:///foo.sv", "v2", makeResult(3));
    CHECK(cache.isUpToDate("file:///foo.sv", "v2"));
    CHECK(cache.get("file:///foo.sv").parseErrors.size() == 3);
}

TEST_CASE("evict removes the entry", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///foo.sv", "content", makeResult(0));
    cache.evict("file:///foo.sv");
    CHECK(!cache.isUpToDate("file:///foo.sv", "content"));
}

TEST_CASE("evict of unknown URI is a no-op", "[compiler][parse-cache]") {
    ParseCache cache;
    CHECK_NOTHROW(cache.evict("file:///nonexistent.sv"));
}

TEST_CASE("multiple URIs are cached independently", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///a.sv", "content-a", makeResult(0));
    cache.store("file:///b.sv", "content-b", makeResult(2));

    CHECK(cache.isUpToDate("file:///a.sv", "content-a"));
    CHECK(cache.isUpToDate("file:///b.sv", "content-b"));
    CHECK(!cache.isUpToDate("file:///a.sv", "content-b"));
    CHECK(!cache.isUpToDate("file:///b.sv", "content-a"));
}

TEST_CASE("evict one URI leaves others intact", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///a.sv", "content-a", makeResult(0));
    cache.store("file:///b.sv", "content-b", makeResult(1));
    cache.evict("file:///a.sv");

    CHECK(!cache.isUpToDate("file:///a.sv", "content-a"));
    CHECK(cache.isUpToDate("file:///b.sv", "content-b"));
}

TEST_CASE("empty content is cached correctly", "[compiler][parse-cache]") {
    ParseCache cache;
    cache.store("file:///empty.sv", "", makeResult(0));
    CHECK(cache.isUpToDate("file:///empty.sv", ""));
    CHECK(!cache.isUpToDate("file:///empty.sv", " "));
}
