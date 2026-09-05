#include <catch2/catch_test_macros.hpp>
#include "lsp/completion.h"
#include "db/database.h"
#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Latency comparison: fuzzy-matching completion (plan.md §6.11's default, on)
// vs. the strict-prefix disabled path, over a large candidate set. This is
// not a correctness test -- test_completion.cpp's own fuzzy-toggle cases
// already cover that both paths return the right *items*; this is purely
// about how long each path takes to build the response.
//
// Tagged [.] (Catch2's "hidden" convention): excluded from the default
// `unit_tests` run so a slow/noisy machine can't flake the suite. Run it
// explicitly with `unit_tests "[completion-latency]"`.
// ---------------------------------------------------------------------------

namespace {

lsp::CompletionParams makeParams(const std::string& path, unsigned line, unsigned col) {
    lsp::CompletionParams p;
    p.textDocument.uri   = lsp::DocumentUri::fromPath(path);
    p.position.line      = line;
    p.position.character = col;
    return p;
}

// Populates `sdb` with `count` top-level Signal symbols under one file, all
// sharing the "sig_" prefix with a zero-padded numeric suffix. Every one of
// them is a literal-prefix match for the "sig" query used below, so both
// modes keep the whole set -- what's being timed is each mode's own
// per-candidate scoring/sorting overhead, not how many candidates survive.
void populateManySymbols(SymbolDatabase& sdb, int count) {
    std::vector<ParseRecord> records;
    records.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "sig_%05d", i);
        records.push_back({ParseRecordKind::Signal, buf, i + 1, 4, "", "", 0, ""});
    }
    sdb.replaceSymbols(sdb.upsertFile("/latency_test.sv", "h"), records);
}

template <typename Fn>
long long timeItMicros(int iterations, Fn&& fn) {
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) fn();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

} // namespace

TEST_CASE("CompletionProvider latency: fuzzy-enabled vs. fuzzy-disabled over 5000 symbols",
          "[completion-latency][.]")
{
    Database db(":memory:");
    db.initSchema();
    SymbolDatabase sdb(db);
    populateManySymbols(sdb, 5000);

    const std::string text = "sig";
    auto params = makeParams("/latency_test.sv", 0, 3);
    const int iterations = 50;

    // Warm-up call for each path (allocator/page-fault/first-call overhead)
    // so the timed loop measures steady-state cost, not one-time setup.
    REQUIRE_FALSE(CompletionProvider::getCompletion(params, sdb, text, true).isNull());
    REQUIRE_FALSE(CompletionProvider::getCompletion(params, sdb, text, false).isNull());

    long long fuzzyMicros = timeItMicros(iterations, [&] {
        auto result = CompletionProvider::getCompletion(params, sdb, text, true);
        REQUIRE_FALSE(result.isNull());
    });
    long long strictMicros = timeItMicros(iterations, [&] {
        auto result = CompletionProvider::getCompletion(params, sdb, text, false);
        REQUIRE_FALSE(result.isNull());
    });

    WARN("fuzzy-enabled:  " << (fuzzyMicros / iterations) << " us/call ("
         << fuzzyMicros << " us / " << iterations << " calls)");
    WARN("fuzzy-disabled: " << (strictMicros / iterations) << " us/call ("
         << strictMicros << " us / " << iterations << " calls)");

    // Sanity bound only -- not a strict fuzzy-vs-strict comparison
    // (relative timing is noisy on a shared/CI machine, and the fuzzy path
    // does strictly more work per candidate by design, so it being slower
    // is expected, not a regression). This just guards against a
    // pathological blowup in either path (e.g. an accidental O(n^2) sort
    // comparator). The bound is deliberately generous (2s/call): a debug
    // build has ASan+UBSan enabled, which this project's own build docs
    // note can be 10-100x slower than release on real workloads -- measured
    // at ~150-180ms/call for 5000 symbols under debug/ASan, comfortably
    // inside this bound with headroom for slower CI hardware.
    CHECK(fuzzyMicros / iterations < 2'000'000);
    CHECK(strictMicros / iterations < 2'000'000);
}
