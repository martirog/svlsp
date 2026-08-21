#include "compiler/sv_preprocessor.h"
#include "compiler/compiler_directive_stripper.h"
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct MacroDef {
    bool isFunctionLike{false};
    std::vector<std::string> params;
    // Parallel to `params`; nullopt means that parameter has no default and
    // must be supplied at every invocation (e.g. `define M(A, B=default_expr)).
    std::vector<std::optional<std::string>> defaults;
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
    std::vector<MacroRecord> macroRecords{};
    std::vector<SourceLine> sourceMap{};

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

// Splits `line` into a macro-expandable code portion and a literal trailing
// `// comment` portion (comment includes the "//" itself and everything
// after — kept verbatim, never macro-expanded). Naive, like
// stripLineComment: doesn't account for "//" appearing inside a string
// literal.
static std::pair<std::string_view, std::string_view> splitLineComment(std::string_view line) {
    size_t pos = line.find("//");
    if (pos == std::string_view::npos) return {line, {}};
    return {line.substr(0, pos), line.substr(pos)};
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
    bool inString = false;
    std::string cur;
    std::vector<std::string> args;
    while (i < src.size() && depth > 0) {
        char c = src[i++];
        if (inString) {
            cur += c;
            if (c == '\\' && i < src.size()) { cur += src[i++]; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if      (c == '"') { inString = true; cur += c; }
        else if (c == '(' || c == '{' || c == '[') { ++depth; cur += c; }
        else if (c == ')' || c == '}' || c == ']') { if (--depth > 0) cur += c; }
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
                              std::vector<std::string>& errors, int depth,
                              std::vector<ColShift>* colShifts = nullptr);

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
    if (args.size() > def.params.size()) {
        errors.push_back("macro `" + name + "`: expected at most " +
                         std::to_string(def.params.size()) + " args, got " +
                         std::to_string(args.size()));
        return "";
    }
    // Any params beyond the supplied args must have a default value.
    for (size_t k = args.size(); k < def.params.size(); ++k) {
        if (k < def.defaults.size() && def.defaults[k].has_value()) {
            args.push_back(*def.defaults[k]);
        } else {
            errors.push_back("macro `" + name + "`: missing required argument `" +
                             def.params[k] + "`");
            return "";
        }
    }
    // Step 1: replace bare param identifiers in the body with arg text
    std::string substituted = substituteParams(def.body, def.params, args);
    // Step 2: expand any backtick macro invocations in the substituted body
    return expandStr(substituted, macros, errors, depth + 1);
}

// Stringification (`"). `src[i]` must be the backtick of a `` `" `` marker
// on entry. Scans for the matching closing `` `" ``, macro-expands the text
// between the two markers (so a stringified macro parameter, already
// substituted by substituteParams before expandStr runs, or a nested macro
// invocation, resolves before quoting), backslash-escapes any `"`/`\` in the
// result so it forms a valid string literal, and wraps it in real double
// quotes. Token-pasting (` `` `) inside a stringification span is not
// supported (see the class doc comment) -- only plain text and further
// macro invocations are handled.
static std::optional<std::string> tryStringify(const std::string& src, size_t& i,
                                                 const MacroMap& macros,
                                                 std::vector<std::string>& errors, int depth) {
    size_t closeAt = std::string::npos;
    for (size_t j = i + 2; j + 1 < src.size(); ++j) {
        if (src[j] == '`' && src[j + 1] == '"') { closeAt = j; break; }
    }
    if (closeAt == std::string::npos) return std::nullopt;

    std::string inner = src.substr(i + 2, closeAt - (i + 2));
    std::string expandedInner = expandStr(inner, macros, errors, depth + 1);
    std::string escaped;
    escaped.reserve(expandedInner.size());
    for (char c : expandedInner) {
        if (c == '\\' || c == '"') escaped += '\\';
        escaped += c;
    }
    i = closeAt + 2; // skip the closing `"
    return '"' + escaped + '"';
}

static std::string expandStr(const std::string& src, const MacroMap& macros,
                              std::vector<std::string>& errors, int depth,
                              std::vector<ColShift>* colShifts) {
    std::string result;
    size_t i = 0;
    int delta = 0; // cumulative output->original column delta, top-level calls only
    while (i < src.size()) {
        if (src[i] != '`') { result += src[i++]; continue; }
        size_t invocationStart = i;
        if (i + 1 < src.size() && src[i + 1] == '"') {
            auto stringified = tryStringify(src, i, macros, errors, depth);
            if (!stringified) {
                errors.push_back("unterminated stringification `\"");
                result += src.substr(i);
                i = src.size();
                break;
            }
            result += *stringified;
            if (colShifts) {
                int invocationLen = static_cast<int>(i - invocationStart);
                int replacementLen = static_cast<int>(stringified->size());
                delta += invocationLen - replacementLen;
                colShifts->push_back({static_cast<int>(result.size()), delta});
            }
            continue;
        }
        ++i; // skip backtick
        if (i >= src.size() || !isIdentChar(src[i])) { result += '`'; continue; }
        std::string name = readIdent(src, i);
        std::string expansion = expandMacroCall(name, src, i, macros, errors, depth);
        result += expansion;
        // Record a column-drift breakpoint at the point right after the
        // replacement text so later columns on this line (in the caller's
        // original line, not a recursively-expanded macro body) can be
        // mapped back. Only meaningful for the top-level per-line call —
        // recursive calls into a macro's own body pass colShifts == nullptr.
        if (colShifts) {
            int invocationLen = static_cast<int>(i - invocationStart);
            int replacementLen = static_cast<int>(expansion.size());
            delta += invocationLen - replacementLen;
            colShifts->push_back({static_cast<int>(result.size()), delta});
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// `define parsing
// ---------------------------------------------------------------------------

struct ParsedMacro { std::string name; std::string body; };

// `rest` is everything after "`define". Returns {name, body} on success, nullopt on error.
static std::optional<ParsedMacro> parseMacroDefinition(std::string_view rest, MacroMap& macros,
                                                        std::vector<std::string>& errors) {
    std::string line(rest);
    size_t i = 0;
    skipSpaces(line, i);
    std::string name = readIdent(line, i);
    if (name.empty()) { errors.push_back("`define: missing macro name"); return std::nullopt; }

    MacroDef def;
    // Function-like: '(' immediately after name with no intervening space
    if (i < line.size() && line[i] == '(') {
        def.isFunctionLike = true;
        ++i; // skip '('
        while (i < line.size() && line[i] != ')') {
            skipSpaces(line, i);
            std::string param = readIdent(line, i);
            if (param.empty()) {
                // Unrecognized character where a parameter name was expected
                // (e.g. a stray token) -- bail out rather than looping forever
                // without making progress through `line`.
                errors.push_back("`define: malformed parameter list for `" + name + "`");
                break;
            }
            def.params.push_back(param);
            skipSpaces(line, i);
            // Optional default value: NAME=expr, up to the next top-level
            // ',' or ')' (nested parens in the default, e.g. a function call
            // like `RO=uvm_get_report_object()`, don't end it early).
            std::optional<std::string> defaultVal;
            if (i < line.size() && line[i] == '=') {
                ++i; // skip '='
                skipSpaces(line, i);
                size_t start = i;
                int parenDepth = 0;
                while (i < line.size() &&
                       !(parenDepth == 0 && (line[i] == ',' || line[i] == ')'))) {
                    if (line[i] == '(') ++parenDepth;
                    else if (line[i] == ')') --parenDepth;
                    ++i;
                }
                defaultVal = std::string(trimSV(std::string_view(line).substr(start, i - start)));
            }
            def.defaults.push_back(std::move(defaultVal));
            skipSpaces(line, i);
            if (i < line.size() && line[i] == ',') ++i;
        }
        if (i < line.size()) ++i; // skip ')'
    }
    skipSpaces(line, i);
    def.body = std::string(stripLineComment(std::string_view(line).substr(i)));
    std::string body = def.body;
    macros[name] = std::move(def);
    return ParsedMacro{std::move(name), std::move(body)};
}

// ---------------------------------------------------------------------------
// Multi-line macro invocations (no backslash continuation)
// ---------------------------------------------------------------------------

// Real SV sources (UVM in particular) commonly split a function-like macro
// invocation's argument list across several physical lines purely via open
// parens/braces, with no trailing '\' -- unlike `define bodies, which the
// LRM requires to use backslash-continuation. Scans `line` for invocations
// of macros already known to be function-like in `macros`, and reports
// whether the *last* such invocation's argument-list depth (paren/brace/
// bracket, string-literal aware, mirroring parseInvokeArgs) is still open
// at end of line -- i.e. more physical lines must be appended before the
// line can be handed to expandStr.
static bool hasUnterminatedInvocation(const std::string& line, const MacroMap& macros) {
    size_t i = 0;
    while (i < line.size()) {
        if (line[i] != '`') { ++i; continue; }
        ++i;
        if (i >= line.size() || !isIdentChar(line[i])) continue;
        std::string name = readIdent(line, i);
        auto it = macros.find(name);
        if (it == macros.end() || !it->second.isFunctionLike) continue;
        size_t j = i;
        skipSpaces(line, j);
        if (j >= line.size() || line[j] != '(') continue;
        int depth = 0;
        bool inString = false;
        for (; j < line.size(); ++j) {
            char c = line[j];
            if (inString) {
                if (c == '\\' && j + 1 < line.size()) { ++j; continue; }
                if (c == '"') inString = false;
                continue;
            }
            if      (c == '"') inString = true;
            else if (c == '(' || c == '{' || c == '[') ++depth;
            else if (c == ')' || c == '}' || c == ']') { --depth; if (depth == 0) break; }
        }
        if (depth > 0) return true; // ran off the end of the line still open
        i = j + 1; // continue scanning for further invocations on this line
    }
    return false;
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

    // Pass 1 (`__FILE__`/`__LINE__` substitution + metadata-directive strip)
    // only ever runs once, on the top-level file, before pass 2 begins.
    // Included files reach here as raw, unstripped text, so run pass 1 on
    // each one now -- otherwise `__FILE__`/`__LINE__` inside an included
    // file are never resolved and fall through to pass 2 as literal,
    // undefined macro invocations. Line count is preserved (stripped
    // directives become blank lines), so the source map stays valid.
    auto stripped = CompilerDirectiveStripper::strip(ss.str(), found);

    ctx.includeStack.push_back(found);
    processSource(stripped.source, found, ctx, depth + 1);
    ctx.includeStack.pop_back();
}

// ---------------------------------------------------------------------------
// Line-level processing
// ---------------------------------------------------------------------------

static void processSource(const std::string& source, const std::string& filepath,
                           Ctx& ctx, int depth) {
    std::istringstream iss(source);
    std::string line;
    int lineNo = 0;

    // Emit one output line (content + newline) and record its origin in the source map.
    // depth == 0 means we are in the primary compiled file; use "" so callers can
    // distinguish primary-file lines from included-file lines with a simple empty check.
    const std::string mapFile = depth > 0 ? filepath : std::string{};
    // `atLine < 0` means "use the current lineNo" (the common case); an
    // explicit value lets a multi-line-merged invocation's real content be
    // attributed to the line it *started* on, even though `lineNo` has since
    // advanced past the continuation lines consumed to complete it.
    auto emitLine = [&](const std::string& content, std::vector<ColShift> shifts = {},
                         int atLine = -1) {
        ctx.output += content;
        ctx.output += '\n';
        ctx.sourceMap.push_back({mapFile, atLine < 0 ? lineNo : atLine, std::move(shifts)});
    };
    auto emitBlank = [&](int atLine = -1) {
        ctx.output += '\n';
        ctx.sourceMap.push_back({mapFile, atLine < 0 ? lineNo : atLine});
    };

    // Appends further physical lines onto `curLine` for as long as it ends
    // mid an open function-like macro invocation, advancing `lineNo` as it
    // consumes each one. Returns the merged text plus the original line
    // number of every continuation line consumed (in order) -- the caller
    // emits the real (merged) content first, attributed to the *first*
    // line, then a blank for each continuation line, preserving the
    // one-output-line-per-physical-input-line invariant the rest of the
    // pipeline (translateLine, ANTLR line numbers) depends on. Note this
    // means any token that lands specifically on a continuation line still
    // gets attributed to the first line for diagnostics/hover purposes --
    // an accepted imprecision, matching how multi-line `define bodies
    // already attribute their whole body to the starting line.
    auto mergeInvocationContinuation =
        [&](std::string curLine) -> std::pair<std::string, std::vector<int>> {
        std::vector<int> continuationLines;
        while (true) {
            auto [code, comment] = splitLineComment(curLine);
            (void)comment;
            if (!hasUnterminatedInvocation(std::string(code), ctx.macros)) break;
            std::string contLine;
            if (!std::getline(iss, contLine)) break;
            ++lineNo;
            if (!contLine.empty() && contLine.back() == '\r') contLine.pop_back();
            curLine += contLine;
            continuationLines.push_back(lineNo);
        }
        return {curLine, continuationLines};
    };

    while (std::getline(iss, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Locate first non-whitespace character
        size_t ws = line.find_first_not_of(" \t");
        bool startsWithBacktick = (ws != std::string::npos && line[ws] == '`');

        if (!startsWithBacktick) {
            // Regular source line: expand macros if outputting. Only the code
            // portion before a trailing `//` comment is scanned for macro
            // invocations -- backtick-looking text inside a comment (common
            // in doc comments showing example macro usage) must not be
            // treated as a real invocation.
            if (ctx.isOutputting()) {
                int firstLineNo = lineNo;
                auto [merged, contLines] = mergeInvocationContinuation(line);
                auto [code, comment] = splitLineComment(merged);
                std::vector<ColShift> shifts;
                std::string expanded =
                    expandStr(std::string(code), ctx.macros, ctx.errors, 0, &shifts);
                emitLine(expanded + std::string(comment), std::move(shifts), firstLineNo);
                for (int ln : contLines) emitBlank(ln);
            } else {
                emitBlank();
            }
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
            emitBlank();
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
            emitBlank();
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
            emitBlank();
            continue;
        }
        if (dir == "endif") {
            if (ctx.condStack.empty()) ctx.errors.push_back("`endif without `ifdef");
            else ctx.condStack.pop_back();
            emitBlank();
            continue;
        }

        // ---- Non-conditional directives (skip when not outputting) ----

        if (!ctx.isOutputting()) { emitBlank(); continue; }

        if (dir == "define") {
            // A `define body may span multiple physical lines via backslash-
            // continuation (same rule as the C preprocessor): a trailing '\'
            // deletes the following newline, splicing the next physical line
            // directly onto the end of this one (no separator inserted). Each
            // consumed physical line still needs its own source-map entry, so
            // the define line's blank is emitted first, then one more per
            // continuation line consumed, before parsing the merged body.
            int defineLine = lineNo;
            std::string mergedRest(rest);
            emitBlank();
            while (!mergedRest.empty() && mergedRest.back() == '\\') {
                mergedRest.pop_back();
                std::string contLine;
                if (!std::getline(iss, contLine)) break;
                ++lineNo;
                if (!contLine.empty() && contLine.back() == '\r') contLine.pop_back();
                mergedRest += contLine;
                emitBlank();
            }
            if (auto opt = parseMacroDefinition(mergedRest, ctx.macros, ctx.errors))
                ctx.macroRecords.push_back({opt->name, opt->body, defineLine});
        } else if (dir == "undef") {
            ctx.macros.erase(std::string(trimSV(stripLineComment(rest))));
            emitBlank();
        } else if (dir == "undefineall") {
            ctx.macros.clear();
            emitBlank();
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
                emitBlank();
            } else {
                processInclude(filename, filepath, ctx, depth);
                // No blank line: included content replaces the `include line.
                // The recursive processSource call pushed source map entries for it.
            }
        } else {
            // Unknown directive starting the line — try macro expansion of the whole line
            int firstLineNo = lineNo;
            auto [merged, contLines] = mergeInvocationContinuation(line);
            std::vector<ColShift> shifts;
            std::string expanded = expandStr(merged, ctx.macros, ctx.errors, 0, &shifts);
            emitLine(expanded, std::move(shifts), firstLineNo);
            for (int ln : contLines) emitBlank(ln);
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
        ctx.macros[name] = MacroDef{.isFunctionLike = false, .body = value};

    processSource(source, filepath, ctx, 0);

    if (!ctx.condStack.empty())
        ctx.errors.push_back("unterminated `ifdef/`ifndef block");

    return {std::move(ctx.output), std::move(ctx.errors), std::move(ctx.macroRecords),
            std::move(ctx.sourceMap)};
}
