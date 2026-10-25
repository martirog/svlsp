// grammar_profile -- parse preprocessed SystemVerilog with a parser generated
// from a grammar copy and report what the parse cost (plan.md §6.33 C2).
//
//   grammar_profile [options] file.pp.sv
//     --sll          SLL prediction (default: LL, what svlsp uses)
//     --profile      ANTLR's per-decision profiler (setProfile); prints the
//                    top decisions by prediction time
//     --top N        rows in the decision table (default 15)
//     --tsv FILE     every decision with any invocation, tab-separated
//     --repeat N     parse N times in one process (run 2+ is DFA-warm)
//     --slice LINES  cut the input after the first `endclass` line at or
//                    after LINES and close it with `endpackage` (a UVM
//                    slice that is still a valid package)
//     --errors N     print the first N syntax errors (default 5)
//     --sort ctx     order the table by context sensitivities instead of
//                    time: the decisions where SLL conflicted and full LL
//                    then chose one alternative -- where an SLL parse can
//                    go wrong (LL runs only)
//     --where D,...  print where those happened for decisions D (line:col
//                    in the input and the tokens SLL looked at)
//     --samples N    locations per --where decision (default 8)
//
// The parse is the walker's (SvTreeWalker::walk): a fresh lexer, token
// stream and parser per run, `source_text` as the start rule, ANTLR's
// default error strategy. Times are wall clock for the parse alone.
#include "SvLexer.h"
#include "SvParser.h"
#include "antlr4-runtime.h"
#include "atn/ParseInfo.h"
#include "atn/ParserATNSimulator.h"
#include "atn/PredictionMode.h"
#include "slice.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CountingListener : antlr4::BaseErrorListener {
    size_t count = 0;
    size_t printLimit = 5;
    void syntaxError(antlr4::Recognizer*, antlr4::Token*, size_t line, size_t col,
                     const std::string& msg, std::exception_ptr) override {
        if (count++ < printLimit)
            std::cout << "  error " << line << ":" << col << " " << msg.substr(0, 160) << "\n";
    }
};

long peakRssMb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind("VmHWM:", 0) == 0) return std::stol(line.substr(6)) / 1024;
    return -1;
}

void usage() {
    std::cerr << "usage: grammar_profile [--sll] [--profile] [--top N] [--tsv FILE] "
                 "[--repeat N] [--slice LINES] [--errors N] [--sort ctx|time] [--where D,...] "
                 "[--samples N] file.pp.sv\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool sll = false, profile = false, sortCtx = false;
    size_t top = 15, repeat = 1, slice = 0, errors = 5, samples = 8;
    std::vector<size_t> where;
    std::string tsv, file;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--sll") sll = true;
        else if (a == "--profile") profile = true;
        else if (a == "--top") top = std::stoul(next());
        else if (a == "--tsv") tsv = next();
        else if (a == "--repeat") repeat = std::stoul(next());
        else if (a == "--slice") slice = std::stoul(next());
        else if (a == "--errors") errors = std::stoul(next());
        else if (a == "--samples") samples = std::stoul(next());
        else if (a == "--sort") {
            const auto key = next();
            if (key != "ctx" && key != "time") { usage(); return 2; }
            sortCtx = key == "ctx";
        } else if (a == "--where") {
            std::stringstream list(next());
            for (std::string d; std::getline(list, d, ',');) where.push_back(std::stoul(d));
        }
        else if (file.empty() && a[0] != '-') file = a;
        else { usage(); return 2; }
    }
    if (file.empty()) { usage(); return 2; }

    std::ifstream in(file);
    if (!in) { std::cerr << "grammar_profile: cannot read " << file << "\n"; return 1; }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (slice) text = sliceAt(text, slice);
    const auto lineCount = std::count(text.begin(), text.end(), '\n');
    std::cout << file << ": " << lineCount << " lines, " << text.size() << " bytes, "
              << (sll ? "SLL" : "LL") << (profile ? ", profiled" : "") << "\n";

    size_t totalErrors = 0;
    for (size_t run = 1; run <= repeat; ++run) {
        antlr4::ANTLRInputStream input(text);
        SvLexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        tokens.fill();
        SvParser parser(&tokens);
        CountingListener listener;
        listener.printLimit = errors;
        lexer.removeErrorListeners();
        lexer.addErrorListener(&listener);
        parser.removeErrorListeners();
        parser.addErrorListener(&listener);
        if (profile) parser.setProfile(true);
        parser.getInterpreter<antlr4::atn::ParserATNSimulator>()->setPredictionMode(
            sll ? antlr4::atn::PredictionMode::SLL : antlr4::atn::PredictionMode::LL);

        const auto t0 = std::chrono::steady_clock::now();
        parser.source_text();
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        totalErrors += listener.count;
        std::printf("run %zu: %.2f s, %zu tokens, %zu syntax errors, peak RSS %ld MB\n", run,
                    secs, tokens.size(), listener.count, peakRssMb());

        if (!profile) continue;
        // DecisionInfo is not assignable: sort indices into it.
        const auto infos = parser.getParseInfo().getDecisionInfo();
        long long total = 0;
        for (const auto& d : infos) total += d.timeInPrediction;
        std::vector<size_t> order(infos.size());
        for (size_t k = 0; k < order.size(); ++k) order[k] = k;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (sortCtx && infos[a].contextSensitivities.size() != infos[b].contextSensitivities.size())
                return infos[a].contextSensitivities.size() > infos[b].contextSensitivities.size();
            return infos[a].timeInPrediction > infos[b].timeInPrediction;
        });
        const auto& ruleNames = parser.getRuleNames();
        const auto& atn = parser.getATN();
        auto ruleOf = [&](size_t decision) {
            return ruleNames[atn.getDecisionState(decision)->ruleIndex];
        };
        std::printf("  prediction total %.2f s\n", total / 1e9);
        size_t ctxTotal = 0;
        for (const auto& d : infos) ctxTotal += d.contextSensitivities.size();
        std::printf("  context sensitivities %zu\n", ctxTotal);
        std::printf("  %8s %-32s %6s %9s %10s %9s %9s %8s %6s %7s\n", "decision", "rule",
                    "share", "ms", "invocs", "LL-fallbk", "SLL-max", "LL-max", "ambig", "ctxsens");
        for (size_t k = 0; k < std::min(top, order.size()); ++k) {
            const auto& d = infos[order[k]];
            std::printf("  %8zu %-32s %5.1f%% %9.1f %10lld %9lld %9lld %8lld %6zu %7zu\n",
                        d.decision, ruleOf(d.decision).c_str(),
                        total ? 100.0 * d.timeInPrediction / total : 0.0, d.timeInPrediction / 1e6,
                        d.invocations, d.LL_Fallback, d.SLL_MaxLook, d.LL_MaxLook,
                        d.ambiguities.size(), d.contextSensitivities.size());
        }
        for (const size_t w : where) {
            if (w >= infos.size()) continue;
            const auto& events = infos[w].contextSensitivities;
            std::printf("  decision %zu (%s): %zu context sensitivities\n", w, ruleOf(w).c_str(),
                        events.size());
            for (size_t k = 0; k < std::min(samples, events.size()); ++k) {
                const auto& e = events[k];
                const auto* start = tokens.get(e.startIndex);
                std::string text = tokens.getText(antlr4::misc::Interval(e.startIndex, e.stopIndex));
                for (auto& c : text) if (c == '\n' || c == '\t') c = ' ';
                if (text.size() > 110) text = text.substr(0, 110) + "...";
                std::printf("    %zu:%zu  %s\n", start->getLine(), start->getCharPositionInLine(),
                            text.c_str());
            }
        }
        if (!tsv.empty()) {
            std::ofstream out(repeat > 1 ? tsv + "." + std::to_string(run) : tsv);
            out << "decision\trule\tns\tinvocations\tll_fallback\tsll_total_look\tsll_max_look"
                   "\tll_total_look\tll_max_look\tambiguities\terrors\tcontext_sensitivities\n";
            for (const size_t k : order) {
                const auto& d = infos[k];
                if (!d.invocations) continue;
                out << d.decision << '\t' << ruleOf(d.decision) << '\t' << d.timeInPrediction
                    << '\t' << d.invocations << '\t' << d.LL_Fallback << '\t' << d.SLL_TotalLook
                    << '\t' << d.SLL_MaxLook << '\t' << d.LL_TotalLook << '\t' << d.LL_MaxLook
                    << '\t' << d.ambiguities.size() << '\t' << d.errors.size() << '\t'
                    << d.contextSensitivities.size() << '\n';
            }
        }
    }
    return totalErrors ? 1 : 0;
}
