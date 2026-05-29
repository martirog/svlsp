#include "compiler/compiler_directive_stripper.h"
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

StripResult CompilerDirectiveStripper::strip(const std::string& source,
                                              const std::string& filepath) {
    std::string result;
    std::vector<DirectiveRecord> directives;
    result.reserve(source.size());

    std::istringstream iss(source);
    std::string line;
    int lineNum = 0;

    while (std::getline(iss, line)) {
        ++lineNum;
        substituteBuiltins(line, filepath, lineNum);
        if (auto rec = matchDirective(line, lineNum)) {
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

    return {std::move(result), std::move(directives)};
}
