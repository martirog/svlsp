#include "compiler/compiler_directive_stripper.h"
#include <algorithm>
#include <optional>
#include <sstream>
#include <string_view>

static void replaceAll(std::string& s, std::string_view from, std::string_view to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static void substituteBuiltins(std::string& line, const std::string& filepath, int lineNum) {
    replaceAll(line, "`__FILE__", "\"" + filepath + "\"");
    replaceAll(line, "`__LINE__", std::to_string(lineNum));
}

static std::optional<DirectiveRecord> matchDirective(const std::string& line, int lineNum) {
    size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos || line[pos] != '`') return std::nullopt;

    std::string_view sv(line.data() + pos + 1, line.size() - pos - 1);

    auto tryMatch = [&](std::string_view keyword, DirectiveKind kind, bool hasValue)
        -> std::optional<DirectiveRecord> {
        if (!sv.starts_with(keyword)) return std::nullopt;
        if (sv.size() > keyword.size()) {
            char c = sv[keyword.size()];
            if (c != ' ' && c != '\t' && c != '\r') return std::nullopt;
        }
        std::string value;
        if (hasValue && sv.size() > keyword.size()) {
            std::string_view rest = sv.substr(keyword.size());
            size_t vstart = rest.find_first_not_of(" \t");
            if (vstart != std::string_view::npos) {
                rest = rest.substr(vstart);
                // Strip trailing line comment
                size_t comment = rest.find("//");
                if (comment != std::string_view::npos) rest = rest.substr(0, comment);
                size_t vend = rest.find_last_not_of(" \t\r");
                value = std::string(vend != std::string_view::npos
                                        ? rest.substr(0, vend + 1)
                                        : rest);
            }
        }
        return DirectiveRecord{kind, value, lineNum};
    };

    // clang-format off
    if (auto r = tryMatch("timescale",          DirectiveKind::Timescale,         true))  return r;
    if (auto r = tryMatch("default_nettype",    DirectiveKind::DefaultNettype,    true))  return r;
    if (auto r = tryMatch("celldefine",         DirectiveKind::Celldefine,        false)) return r;
    if (auto r = tryMatch("endcelldefine",      DirectiveKind::Endcelldefine,     false)) return r;
    if (auto r = tryMatch("unconnected_drive",  DirectiveKind::UnconnectedDrive,  true))  return r;
    if (auto r = tryMatch("nounconnected_drive",DirectiveKind::NounconnectedDrive,false)) return r;
    if (auto r = tryMatch("resetall",           DirectiveKind::Resetall,          false)) return r;
    if (auto r = tryMatch("begin_keywords",     DirectiveKind::BeginKeywords,     true))  return r;
    if (auto r = tryMatch("end_keywords",       DirectiveKind::EndKeywords,       false)) return r;
    if (auto r = tryMatch("pragma",             DirectiveKind::Pragma,            true))  return r;
    if (auto r = tryMatch("line",               DirectiveKind::Line,              true))  return r;
    // clang-format on

    return std::nullopt;
}

// The words of a `pragma's value: "protect begin_protected" -> {protect,
// begin_protected}. Keyword arguments (`encoding=(...)`) are words too.
static std::vector<std::string_view> words(std::string_view v) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < v.size()) {
        while (i < v.size() && (v[i] == ' ' || v[i] == '\t' || v[i] == ',')) ++i;
        const size_t b = i;
        while (i < v.size() && v[i] != ' ' && v[i] != '\t' && v[i] != ',') ++i;
        if (i > b) out.push_back(v.substr(b, i - b));
    }
    return out;
}

static bool isProtectPragma(const DirectiveRecord& rec, std::string_view keyword) {
    if (rec.kind != DirectiveKind::Pragma) return false;
    const auto w = words(rec.value);
    return w.size() >= 2 && w[0] == "protect" &&
           std::find(w.begin() + 1, w.end(), keyword) != w.end();
}

// A line that is exactly `word (VCS's `protected / `endprotected).
static bool isBareDirective(const std::string& line, std::string_view word) {
    const size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos || line[b] != '`') return false;
    const size_t e = line.find_last_not_of(" \t\r");
    return std::string_view(line).substr(b + 1, e - b) == word;
}

StripResult CompilerDirectiveStripper::strip(const std::string& source,
                                              const std::string& filepath) {
    std::string result;
    std::vector<DirectiveRecord> directives;
    std::vector<ProtectedRegion> regions;
    result.reserve(source.size());

    std::istringstream iss(source);
    std::string line;
    int lineNum = 0;
    bool vcsEnvelope = false; // the open region began with `protected

    while (std::getline(iss, line)) {
        ++lineNum;
        // Inside an envelope every line is blanked, up to and with its end.
        if (!regions.empty() && !regions.back().closed) {
            result += '\n';
            const auto rec = matchDirective(line, lineNum);
            if (vcsEnvelope ? isBareDirective(line, "endprotected")
                            : rec && isProtectPragma(*rec, "end_protected")) {
                regions.back().endLine = lineNum;
                regions.back().closed = true;
            }
            continue;
        }
        if (isBareDirective(line, "protected")) {
            regions.push_back({"", lineNum, lineNum, false});
            vcsEnvelope = true;
            result += '\n';
            continue;
        }
        substituteBuiltins(line, filepath, lineNum);
        if (auto rec = matchDirective(line, lineNum)) {
            if (isProtectPragma(*rec, "begin_protected")) {
                regions.push_back({"", lineNum, lineNum, false});
                vcsEnvelope = false;
            }
            directives.push_back(*rec);
            result += '\n';
        } else {
            result += line;
            result += '\n';
        }
    }

    // Restore absence of trailing newline if the original had none
    if (!source.empty() && source.back() != '\n' && !result.empty()) {
        result.pop_back();
    }
    if (!regions.empty() && !regions.back().closed) regions.back().endLine = lineNum;

    return {std::move(result), std::move(directives), std::move(regions)};
}
