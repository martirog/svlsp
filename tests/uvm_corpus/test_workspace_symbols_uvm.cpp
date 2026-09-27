#include <catch2/catch_test_macros.hpp>
#include "lsp/workspace_symbols.h"
#include "uvm_corpus_fixture.h"
#include <algorithm>

// workspace/symbol against the real, full UVM corpus DB. Ground truth is the
// 28-symbol candidate list catalogued in handoff.md (sessions 2/3), each a
// real `class NAME ...` declaration confirmed present at this exact
// file:line by a fresh grep against the corpus while writing this suite.
//
// workspace/symbol does substring/prefix matching (documented session-3
// behavior -- e.g. querying "uvm_reg" matches 71 results across the whole
// reg/ subtree), so every case checks the result set *contains* an entry at
// the expected file:line, not that it's the only result.

namespace {

lsp::WorkspaceSymbolParams makeParams(std::string_view query) {
    lsp::WorkspaceSymbolParams p;
    p.query = query;
    return p;
}

// 1-based line, as in the source file / handoff catalog.
bool containsSymbolAt(const lsp::Array<lsp::WorkspaceSymbol>& syms,
                       const std::string& name, const std::string& file, unsigned line1Based) {
    const std::string expectedPath = expectedUriPath(file);
    return std::any_of(syms.begin(), syms.end(), [&](const lsp::WorkspaceSymbol& s) {
        if (s.name != name) return false;
        if (!std::holds_alternative<lsp::Location>(s.location)) return false;
        const auto& loc = std::get<lsp::Location>(s.location);
        return std::string(loc.uri.path()) == expectedPath &&
               loc.range.start.line == line1Based - 1;
    });
}

void checkCandidate(const std::string& name, const std::string& file, unsigned line1Based) {
    INFO(name << " @ " << file << ":" << line1Based);
    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams(name), uvmCorpusDb());
    REQUIRE_FALSE(result.isNull());
    auto& syms = result.get<lsp::Array<lsp::WorkspaceSymbol>>();
    CHECK(containsSymbolAt(syms, name, file, line1Based));
}

} // namespace

TEST_CASE("workspace/symbol finds all 28 cataloged real UVM classes", "[uvm_corpus][workspace_symbols]") {
    checkCandidate("uvm_component",              "base/uvm_component.svh",     59);
    checkCandidate("uvm_object",                 "base/uvm_object.svh",        61);
    checkCandidate("uvm_root",                   "base/uvm_root.svh",          98);
    checkCandidate("uvm_phase",                   "base/uvm_phase.svh",        147);
    checkCandidate("uvm_objection",               "base/uvm_objection.svh",    79);
    checkCandidate("uvm_report_server",           "base/uvm_report_server.svh", 65);
    checkCandidate("uvm_resource_db",             "base/uvm_resource_db.svh",  66);
    checkCandidate("uvm_config_db",               "base/uvm_config_db.svh",    58);
    checkCandidate("uvm_event",                   "base/uvm_event.svh",        282);
    checkCandidate("uvm_barrier",                 "base/uvm_barrier.svh",      45);
    checkCandidate("uvm_heartbeat",                "base/uvm_heartbeat.svh",   67);
    checkCandidate("uvm_coreservice_t",           "base/uvm_coreservice.svh",  71);
    checkCandidate("uvm_domain",                   "base/uvm_domain.svh",      78);
    checkCandidate("uvm_agent",                    "comps/uvm_agent.svh",      51);
    checkCandidate("uvm_driver",                   "comps/uvm_driver.svh",     58);
    checkCandidate("uvm_monitor",                  "comps/uvm_monitor.svh",    45);
    checkCandidate("uvm_scoreboard",                "comps/uvm_scoreboard.svh", 47);
    checkCandidate("uvm_algorithmic_comparator",   "comps/uvm_algorithmic_comparator.svh", 81);
    checkCandidate("uvm_sequence",                  "seq/uvm_sequence.svh",    47);
    checkCandidate("uvm_sequence_item",             "seq/uvm_sequence_item.svh", 52);
    checkCandidate("uvm_sequencer",                 "seq/uvm_sequencer.svh",   44);
    checkCandidate("uvm_sequence_base",             "seq/uvm_sequence_base.svh", 153);
    checkCandidate("uvm_reg",                       "reg/uvm_reg.svh",         102);
    checkCandidate("uvm_reg_field",                 "reg/uvm_reg_field.svh",   50);
    checkCandidate("uvm_mem",                       "reg/uvm_mem.svh",         57);
    checkCandidate("uvm_reg_block",                 "reg/uvm_reg_block.svh",   40);
    checkCandidate("uvm_analysis_port",             "tlm1/uvm_analysis_port.svh", 68);
    checkCandidate("uvm_tlm_generic_payload",       "tlm2/uvm_tlm2_generic_payload.svh", 114);
}

TEST_CASE("workspace/symbol lists UVM macros from the macros table", "[uvm_corpus][workspace_symbols][macro]") {
    // `define uvm_info(ID, MSG, VERBOSITY) at macros/uvm_message_defines.svh:155.
    checkCandidate("uvm_info", "macros/uvm_message_defines.svh", 155);

    // `uvm_info finds only macros, and `_` in the query is not a wildcard.
    auto result = WorkspaceSymbolsProvider::getWorkspaceSymbols(makeParams("`uvm_info"), uvmCorpusDb());
    REQUIRE_FALSE(result.isNull());
    const auto& syms = result.get<lsp::Array<lsp::WorkspaceSymbol>>();
    CHECK(containsSymbolAt(syms, "uvm_info", "macros/uvm_message_defines.svh", 155));
    for (const auto& s : syms) {
        CHECK(s.containerName.value_or("") == "`define");
        CHECK(s.name.rfind("uvm_info", 0) == 0);
    }
}
