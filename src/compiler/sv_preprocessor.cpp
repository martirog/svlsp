#include "compiler/sv_preprocessor.h"
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct MacroDef {
    bool isFunctionLike{false};
    std::vector<std::string> params;
    std::string body;
};

using MacroMap = std::unordered_map<std::string, MacroDef>;

struct CondEntry {
    bool active;    // should the current branch emit code?
    bool seenTrue;  // has any branch in this block been active?
    bool inElse;    // are we past `else?
};

struct Ctx {
    MacroMap macros{};
    std::vector<CondEntry> condStack{};
    std::vector<std::string> errors{};
    std::vector<std::string> includeStack{}; // cycle detection
    const std::vector<std::string>& includePaths;
    std::string output{};

    bool isOutputting() const {
        for (const auto& e : condStack) if (!e.active) return false;
        return true;
    }
};

// ---------------------------------------------------------------------------
// String / scan helpers
// ---------------------------------------------------------------------------

static bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static std::string_view trimSV(std::string_view sv) {
    size_t l = sv.find_first_not_of(" \t");
    if (l == std::string_view::npos) return {};
    size_t r = sv.find_last_not_of(" \t\r");
    return sv.substr(l, r - l + 1);
}

static std::string_view stripLineComment(std::string_view sv) {
    size_t pos = sv.find("//");
    return pos == std::string_view::npos ? sv : trimSV(sv.substr(0, pos));
}

static std::string readIdent(const std::string& src, size_t& i) {
    size_t start = i;
    while (i < src.size() && isIdentChar(src[i])) ++i;
    return src.substr(start, i - start);
}

static void skipSpaces(const std::string& src, size_t& i) {
    while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) ++i;
}

// Parse function-like invocation args (src[i] must equal '(' on entry).
// Returns trimmed arg strings; i is left after the closing ')'.
static std::vector<std::string> parseInvokeArgs(const std::string& src, size_t& i) {
    assert(i < src.size() && src[i] == '(');
    ++i; // skip '('
    int depth = 1;
    std::string cur;
    std::vector<std::string> args;
    while (i < src.size() && depth > 0) {
        char c = src[i++];
        if      (c == '(') { ++depth; cur += c; }
        else if (c == ')') { if (--depth > 0) cur += c; }
        else if (c == ',' && depth == 1) { args.push_back(std::string(trimSV(cur))); cur.clear(); }
        else    cur += c;
    }
    // Only push the last segment if the arg list was non-empty (avoid phantom empty arg
    // for zero-param macros like `FOO())
    std::string last = std::string(trimSV(cur));
    if (!last.empty() || !args.empty()) args.push_back(last);
    return args;
}

// ---------------------------------------------------------------------------
// Macro expansion
// ---------------------------------------------------------------------------

// Replace bare parameter identifiers in `body` with the corresponding arg
// values. In SV (as in C), macro params appear as plain identifiers in the
// body — not as backtick tokens — so this step is separate from expandStr.
static std::string substituteParams(const std::string& body,
                                     const std::vector<std::string>& params,
                                     const std::vector<std::string>& args) {
    if (params.empty()) return body;
    std::string result;
    size_t i = 0;
    while (i < body.size()) {
        // Only identifier-start chars (alpha or _) begin a substitution candidate
        if (!std::isalpha(static_cast<unsigned char>(body[i])) && body[i] != '_') {
            result += body[i++];
            continue;
        }
        size_t start = i;
        while (i < body.size() && isIdentChar(body[i])) ++i;
        std::string ident = body.substr(start, i - start);
        bool found = false;
        for (size_t k = 0; k < params.size(); ++k) {
            if (params[k] == ident) { result += args[k]; found = true; break; }
        }
        if (!found) result += ident;
    }
    return result;
}

static std::string expandStr(const std::string& src, const MacroMap& macros,
                              std::vector<std::string>& errors, int depth);

static std::string expandMacroCall(const std::string& name,
                                    const std::string& src, size_t& i,
                                    const MacroMap& macros,
                                    std::vector<std::string>& errors, int depth) {
    if (depth > 32) {
        errors.push_back("macro expansion depth exceeded (recursive macro?)");
        return "";
    }
    auto it = macros.find(name);
    if (it == macros.end()) {
        errors.push_back("undefined macro `" + name + "`");
        return "";
    }
    const MacroDef& def = it->second;

    if (!def.isFunctionLike) {
        return expandStr(def.body, macros, errors, depth + 1);
    }

    // Function-like: expect '(' next (after optional spaces)
    skipSpaces(src, i);
    if (i >= src.size() || src[i] != '(') {
        errors.push_back("function-like macro `" + name + "` invoked without arguments");
        return "";
    }
    std::vector<std::string> args = parseInvokeArgs(src, i);
    if (args.size() != def.params.size()) {
        errors.push_back("macro `" + name + "`: expected " +
                         std::to_string(def.params.size()) + " args, got " +
                         std::to_string(args.size()));
        return "";
    }
    // Step 1: replace bare param identifiers in the body with arg text
    std::string substituted = substituteParams(def.body, def.params, args);
    // Step 2: expand any backtick macro invocations in the substituted body
    return expandStr(substituted, macros, errors, depth + 1);
}

static std::string expandStr(const std::string& src, const MacroMap& macros,
                              std::vector<std::string>& errors, int depth) {
    std::string result;
    size_t i = 0;
    while (i < src.size()) {
        if (src[i] != '`') { result += src[i++]; continue; }
        ++i; // skip backtick
        if (i >= src.size() || !isIdentChar(src[i])) { result += '`'; continue; }
        std::string name = readIdent(src, i);
        result += expandMacroCall(name, src, i, macros, errors, depth);
    }
    return result;
}

// ---------------------------------------------------------------------------
// `define parsing
// ---------------------------------------------------------------------------

// `rest` is everything after "`define"
static void parseMacroDefinition(std::string_view rest, MacroMap& macros,
                                  std::vector<std::string>& errors) {
    std::string line(rest);
    size_t i = 0;
    skipSpaces(line, i);
    std::string name = readIdent(line, i);
    if (name.empty()) { errors.push_back("`define: missing macro name"); return; }

    MacroDef def;
    // Function-like: '(' immediately after name with no intervening space
    if (i < line.size() && line[i] == '(') {
        def.isFunctionLike = true;
        ++i; // skip '('
        while (i < line.size() && line[i] != ')') {
            skipSpaces(line, i);
            std::string param = readIdent(line, i);
            if (!param.empty()) def.params.push_back(param);
            skipSpaces(line, i);
            if (i < line.size() && line[i] == ',') ++i;
        }
        if (i < line.size()) ++i; // skip ')'
    }
    skipSpaces(line, i);
    def.body = std::string(stripLineComment(std::string_view(line).substr(i)));
    macros[name] = std::move(def);
}

// ---------------------------------------------------------------------------
// Include resolution
// ---------------------------------------------------------------------------

static void processSource(const std::string& source, const std::string& filepath,
                           Ctx& ctx, int depth);

static void processInclude(const std::string& filename, const std::string& currentFile,
                            Ctx& ctx, int depth) {
    if (depth > 16) {
        ctx.errors.push_back("`include: depth limit exceeded (cycle?)");
        return;
    }

    // Build search list: dir of current file, then explicit include paths
    std::vector<std::string> search;
    if (!currentFile.empty()) {
        fs::path p(currentFile);
        if (p.has_parent_path()) search.push_back(p.parent_path().string());
    }
    for (const auto& d : ctx.includePaths) search.push_back(d);

    // Also try the filename as an absolute/relative path directly
    std::string found;
    if (fs::exists(filename)) {
        found = filename;
    } else {
        for (const auto& dir : search) {
            auto candidate = fs::path(dir) / filename;
            if (fs::exists(candidate)) { found = candidate.string(); break; }
        }
    }

    if (found.empty()) {
        ctx.errors.push_back("`include: cannot find \"" + filename + "\"");
        return;
    }

    for (const auto& p : ctx.includeStack) {
        if (p == found) {
            ctx.errors.push_back("`include: cycle detected for \"" + found + "\"");
            return;
        }
    }

    std::ifstream f(found);
    if (!f.is_open()) {
        ctx.errors.push_back("`include: cannot open \"" + found + "\"");
        return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    ctx.includeStack.push_back(found);
    processSource(ss.str(), found, ctx, depth + 1);
    ctx.includeStack.pop_back();
}

// ---------------------------------------------------------------------------
// Line-level processing
// ---------------------------------------------------------------------------

static void processSource(const std::string& source, const std::string& filepath,
                           Ctx& ctx, int depth) {
    std::istringstream iss(source);
    std::string line;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Locate first non-whitespace character
        size_t ws = line.find_first_not_of(" \t");
        bool startsWithBacktick = (ws != std::string::npos && line[ws] == '`');

        if (!startsWithBacktick) {
            // Regular source line: expand macros if outputting
            if (ctx.isOutputting())
                ctx.output += expandStr(line, ctx.macros, ctx.errors, 0);
            ctx.output += '\n';
            continue;
        }

        // Parse directive name
        size_t i = ws + 1;
        std::string dir = readIdent(line, i);
        std::string_view rest(line.data() + i, line.size() - i);

        // ---- Conditional directives (processed even when skipping) ----

        if (dir == "ifdef" || dir == "ifndef") {
            std::string macName = std::string(trimSV(stripLineComment(rest)));
            bool defined = ctx.macros.count(macName) > 0;
            bool active  = (dir == "ifdef") ? defined : !defined;
            ctx.condStack.push_back({active, active, false});
            ctx.output += '\n';
            continue;
        }
        if (dir == "elsif") {
            if (ctx.condStack.empty()) {
                ctx.errors.push_back("`elsif without `ifdef");
            } else {
                auto& top = ctx.condStack.back();
                if (top.inElse) {
                    ctx.errors.push_back("`elsif after `else");
                } else {
                    std::string macName = std::string(trimSV(stripLineComment(rest)));
                    bool defined = ctx.macros.count(macName) > 0;
                    bool cond = !top.seenTrue && defined;
                    top.active = cond;
                    if (cond) top.seenTrue = true;
                }
            }
            ctx.output += '\n';
            continue;
        }
        if (dir == "else") {
            if (ctx.condStack.empty()) {
                ctx.errors.push_back("`else without `ifdef");
            } else {
                auto& top = ctx.condStack.back();
                if (top.inElse) { ctx.errors.push_back("duplicate `else"); }
                else { top.inElse = true; top.active = !top.seenTrue; }
            }
            ctx.output += '\n';
            continue;
        }
        if (dir == "endif") {
            if (ctx.condStack.empty()) ctx.errors.push_back("`endif without `ifdef");
            else ctx.condStack.pop_back();
            ctx.output += '\n';
            continue;
        }

        // ---- Non-conditional directives (skip when not outputting) ----

        if (!ctx.isOutputting()) { ctx.output += '\n'; continue; }

        if (dir == "define") {
            parseMacroDefinition(rest, ctx.macros, ctx.errors);
            ctx.output += '\n';
        } else if (dir == "undef") {
            ctx.macros.erase(std::string(trimSV(stripLineComment(rest))));
            ctx.output += '\n';
        } else if (dir == "undefineall") {
            ctx.macros.clear();
            ctx.output += '\n';
        } else if (dir == "include") {
            std::string_view r = trimSV(rest);
            std::string filename;
            if (!r.empty() && (r.front() == '"' || r.front() == '<')) {
                char close = r.front() == '"' ? '"' : '>';
                size_t end = r.find(close, 1);
                if (end != std::string_view::npos) filename = std::string(r.substr(1, end - 1));
            }
            if (filename.empty()) {
                ctx.errors.push_back("`include: missing or malformed filename");
                ctx.output += '\n';
            } else {
                processInclude(filename, filepath, ctx, depth);
                // No blank line: included content replaces the `include line
            }
        } else {
            // Unknown directive starting the line — try macro expansion of the whole line
            ctx.output += expandStr(line, ctx.macros, ctx.errors, 0);
            ctx.output += '\n';
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SvPreprocessor::SvPreprocessor(std::vector<std::string> includePaths)
    : m_includePaths(std::move(includePaths)) {}

void SvPreprocessor::define(const std::string& name, const std::string& value) {
    m_predefined.emplace_back(name, value);
}

PreprocessorResult SvPreprocessor::process(const std::string& source,
                                            const std::string& filepath) {
    Ctx ctx{.includePaths = m_includePaths};

    // Seed macro table from predefined macros
    for (const auto& [name, value] : m_predefined)
        ctx.macros[name] = MacroDef{false, {}, value};

    processSource(source, filepath, ctx, 0);

    if (!ctx.condStack.empty())
        ctx.errors.push_back("unterminated `ifdef/`ifndef block");

    return {std::move(ctx.output), std::move(ctx.errors)};
}
