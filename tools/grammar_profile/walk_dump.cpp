// walk_dump -- run svlsp's tree walker (SvTreeWalker::walk, compiled against
// the parser generated from the grammar being profiled) on preprocessed
// SystemVerilog and print everything it records, one line per item, in a
// stable order. Two grammars that give the walker the same trees give the
// same dump, so `diff` of two dumps is the check that a grammar change kept
// the walker's API (plan.md §6.33 C2).
//
//   walk_dump [--ll|--sll] [--slice LINES] file.pp.sv > dump.txt
//
// The parse is svlsp's two-stage one (SLL, LL after a syntax error) unless
// --ll or --sll forces one prediction mode.
//
// Lines and columns are the preprocessed file's (no source map).
#include "compiler/sv_tree_walker.h"
#include "slice.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string esc(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\x1F') out += "\\x1F";
        else out += c;
    }
    return out;
}

void emit(std::vector<std::string>& lines, std::ostream& out) {
    std::sort(lines.begin(), lines.end());
    for (const auto& l : lines) out << l << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    auto prediction = ParsePrediction::TwoStage;
    size_t slice = 0;
    std::string file;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--sll") prediction = ParsePrediction::SLL;
        else if (a == "--ll") prediction = ParsePrediction::LL;
        else if (a == "--slice" && i + 1 < argc) slice = std::stoul(argv[++i]);
        else if (file.empty() && a[0] != '-') file = a;
        else {
            std::cerr << "usage: walk_dump [--ll|--sll] [--slice LINES] file.pp.sv\n";
            return 2;
        }
    }
    std::ifstream in(file);
    if (!in) {
        std::cerr << "walk_dump: cannot read " << file << "\n";
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (slice) text = sliceAt(text, slice);

    const auto t0 = std::chrono::steady_clock::now();
    const auto r = SvTreeWalker::walk(text, {}, true, "", prediction);
    std::cerr << "walk_dump: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
              << " s, " << r.records.size() << " records, " << r.calls.size() << " calls, "
              << r.parseErrors.size() << " errors\n";

    std::vector<std::string> lines;
    for (const auto& p : r.records) {
        std::ostringstream l;
        l << "record " << p.line << ':' << p.column << ' ' << static_cast<int>(p.kind) << ' '
          << p.name << " parent=" << p.parent << " scope=" << p.scope << " end=" << p.endLine
          << ':' << p.endCol << " access=" << p.access << " detail=" << esc(p.detail)
          << " doc=" << esc(p.doc);
        lines.push_back(l.str());
    }
    for (const auto& c : r.calls) {
        std::ostringstream l;
        l << "call " << c.line << ':' << c.column << ' ' << c.calleeName << " scope=" << c.calleeScope
          << " name=" << c.nameLine << ':' << c.nameColumn << " args=";
        for (const auto& a : c.args)
            l << (a.kind == CallArgSlot::Kind::Positional ? "p"
                  : a.kind == CallArgSlot::Kind::Elided   ? "e"
                                                          : "n:" + a.name)
              << ',';
        lines.push_back(l.str());
    }
    for (const auto& i : r.imports)
        lines.push_back("import " + std::to_string(i.line) + ' ' + i.pkgName + "::" + i.item +
                        (i.isExport ? " export" : "") + " scope=" + i.scope);
    for (const auto& i : r.instantiations)
        lines.push_back("instance " + std::to_string(i.line) + ' ' + i.typeName + ' ' + i.instName);
    for (const auto& e : r.parseErrors)
        lines.push_back("error " + std::to_string(e.line) + ':' + std::to_string(e.column) + ' ' +
                        esc(e.message));
    for (const auto& [f, s] : r.includeScopes) lines.push_back("include_scope " + f + ' ' + s);
    emit(lines, std::cout);
    return 0;
}
