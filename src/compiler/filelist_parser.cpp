#include "compiler/filelist_parser.h"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

std::string_view stripComment(std::string_view line) {
    size_t pos = line.find("//");
    return pos == std::string_view::npos ? line : line.substr(0, pos);
}

// Split a (comment-stripped) line into whitespace-separated tokens, honoring
// double-quoted segments (which may contain spaces) as a single token.
std::vector<std::string> tokenizeLine(std::string_view line) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size()) break;
        if (line[i] == '"') {
            ++i;
            size_t start = i;
            while (i < line.size() && line[i] != '"') ++i;
            tokens.emplace_back(line.substr(start, i - start));
            if (i < line.size()) ++i; // skip closing quote
        } else {
            size_t start = i;
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            tokens.emplace_back(line.substr(start, i - start));
        }
    }
    return tokens;
}

// Split a chained "+switch+val1+val2+..." token on '+'. First element is the
// switch name itself (e.g. "define"); the rest are its chained values.
std::vector<std::string> splitChain(const std::string& token) {
    std::vector<std::string> parts;
    size_t i = 1; // skip leading '+'
    size_t start = i;
    for (; i <= token.size(); ++i) {
        if (i == token.size() || token[i] == '+') {
            parts.push_back(token.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::string resolvePath(const std::string& baseDir, const std::string& raw) {
    fs::path p(raw);
    if (p.is_absolute()) return p.lexically_normal().string();
    return (fs::path(baseDir) / p).lexically_normal().string();
}

void parseFile(const std::string& path, const std::string& baseDir,
               std::unordered_set<std::string>& activeStack, ProjectConfig& config) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(path + ": cannot open filelist");
    }
    std::string canonical = fs::canonical(path).string();
    if (activeStack.count(canonical)) {
        throw std::runtime_error(path + ": cycle detected in -f/-F inclusion");
    }
    activeStack.insert(canonical);

    std::ostringstream ss;
    ss << f.rdbuf();
    std::istringstream iss(ss.str());

    std::string line;
    int lineNo = 0;
    while (std::getline(iss, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::vector<std::string> tokens = tokenizeLine(stripComment(line));
        for (size_t t = 0; t < tokens.size(); ++t) {
            const std::string& tok = tokens[t];
            if (tok.empty()) continue;

            auto needArg = [&](const std::string& switchName) -> std::string {
                if (t + 1 >= tokens.size()) {
                    throw std::runtime_error(path + ":" + std::to_string(lineNo) + ": " +
                                              switchName + " requires an argument");
                }
                return tokens[++t];
            };

            if (tok[0] == '+') {
                auto parts = splitChain(tok);
                if (parts.empty()) continue;
                const std::string& sw = parts[0];
                if (sw == "define") {
                    for (size_t k = 1; k < parts.size(); ++k) {
                        size_t eq = parts[k].find('=');
                        if (eq == std::string::npos) config.defines[parts[k]] = "";
                        else config.defines[parts[k].substr(0, eq)] = parts[k].substr(eq + 1);
                    }
                } else if (sw == "incdir") {
                    for (size_t k = 1; k < parts.size(); ++k)
                        config.includeDirs.push_back(resolvePath(baseDir, parts[k]));
                } else if (sw == "libext") {
                    for (size_t k = 1; k < parts.size(); ++k)
                        config.libExtensions.push_back(parts[k]);
                } else {
                    throw std::runtime_error(path + ":" + std::to_string(lineNo) +
                                              ": unsupported switch '" + tok + "'");
                }
            } else if (tok[0] == '-') {
                if (tok == "-f" || tok == "-F") {
                    std::string nestedRaw  = needArg(tok);
                    std::string nestedPath = resolvePath(baseDir, nestedRaw);
                    std::string nestedBase =
                        tok == "-F" ? fs::path(nestedPath).parent_path().string() : baseDir;
                    parseFile(nestedPath, nestedBase, activeStack, config);
                } else if (tok == "-sv" || tok == "-sverilog") {
                    config.mode = SvLanguageMode::SystemVerilog;
                } else if (tok == "-y") {
                    config.libraryDirs.push_back(resolvePath(baseDir, needArg(tok)));
                } else if (tok == "-v") {
                    config.libraryFiles.push_back(resolvePath(baseDir, needArg(tok)));
                } else if (tok == "-svlsp_library_db") {
                    config.libraryDbs.push_back(resolvePath(baseDir, needArg(tok)));
                } else if (tok == "-svlsp_library_db_source") {
                    std::string cfgPath   = resolvePath(baseDir, needArg(tok));
                    std::string cachePath = resolvePath(baseDir, needArg(tok));
                    config.libraryDbSources.push_back({cfgPath, cachePath});
                } else if (tok == "-top") {
                    config.topModule = needArg(tok);
                } else {
                    throw std::runtime_error(path + ":" + std::to_string(lineNo) +
                                              ": unsupported switch '" + tok + "'");
                }
            } else {
                config.files.push_back(resolvePath(baseDir, tok));
            }
        }
    }

    activeStack.erase(canonical);
}

} // namespace

ProjectConfig FilelistParser::parse(const std::string& path, const std::string& baseDir) {
    ProjectConfig config;
    std::unordered_set<std::string> activeStack;
    std::string base = baseDir.empty() ? fs::current_path().string() : baseDir;
    parseFile(path, base, activeStack, config);
    return config;
}
