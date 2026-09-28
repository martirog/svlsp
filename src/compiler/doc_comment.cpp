#include "compiler/doc_comment.h"
#include <algorithm>
#include <sstream>

namespace {

std::string rtrim(std::string s) {
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

std::string ltrim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    return start == std::string::npos ? std::string{} : s.substr(start);
}

// A line of nothing but decoration: `----`, `====`, `****`, `////`, ...
bool isSeparator(const std::string& line) {
    const std::string t = ltrim(rtrim(line));
    return t.size() >= 3 && t.find_first_not_of("-=*/#~_+") == std::string::npos;
}

// A UVM annotation line: `@uvm-ieee 1800.2-2020 auto 13.1.3.3`, `@uvm-compat`, ...
bool isTag(const std::string& line) {
    return ltrim(line).rfind("@uvm", 0) == 0;
}

// The content lines of one comment, markers stripped, indentation kept.
void appendLines(const std::string& raw, std::vector<std::string>& out) {
    std::string text = rtrim(raw);
    if (text.rfind("//", 0) == 0) {
        size_t i = 2;
        while (i < text.size() && text[i] == '/') ++i;
        out.push_back(text.substr(i));
        return;
    }
    if (text.rfind("/*", 0) != 0) return;
    size_t begin = 2;
    while (begin < text.size() && text[begin] == '*') ++begin;
    size_t end = text.size();
    if (end >= begin + 2 && text.compare(end - 2, 2, "*/") == 0) end -= 2;
    std::istringstream body(text.substr(begin, end - begin));
    std::string line;
    bool first = true;
    while (std::getline(body, line)) {
        if (!first) {
            // A continuation line's leading ` * ` gutter is decoration.
            size_t i = line.find_first_not_of(" \t");
            if (i != std::string::npos && line[i] == '*' &&
                (i + 1 == line.size() || line[i + 1] != '/'))
                line = line.substr(i + 1);
            else if (i == std::string::npos)
                line.clear();
        }
        out.push_back(rtrim(line));
        first = false;
    }
}

} // namespace

std::string cleanDocComment(const std::vector<std::string>& comments) {
    std::vector<std::string> lines;
    for (const auto& c : comments) appendLines(c, lines);

    std::vector<std::string> kept;
    for (auto& l : lines) {
        l = rtrim(l);
        if (isSeparator(l) || isTag(l)) continue;
        kept.push_back(l);
    }

    size_t indent = std::string::npos;
    for (const auto& l : kept)
        if (size_t i = l.find_first_not_of(" \t"); i != std::string::npos)
            indent = std::min(indent, i);

    while (!kept.empty() && kept.front().empty()) kept.erase(kept.begin());
    while (!kept.empty() && kept.back().empty()) kept.pop_back();

    std::string doc;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) doc += '\n';
        if (!kept[i].empty()) doc += kept[i].substr(indent);
    }
    return doc;
}
