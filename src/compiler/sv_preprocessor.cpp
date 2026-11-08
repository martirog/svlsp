#include "compiler/sv_preprocessor.h"
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include "compiler/compiler_directive_stripper.h"
#include "compiler/doc_comment.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

struct MacroDef {
    bool isFunctionLike{false};
    std::vector<std::string> params{};
    // Parallel to `params`; nullopt means that parameter has no default and
    // must be supplied at every invocation (e.g. `define M(A, B=default_expr)).
    std::vector<std::optional<std::string>> defaults{};
    std::string body{};
};

using MacroMap = std::unordered_map<std::string, MacroDef>;

struct CondEntry {
    bool active;    // should the current branch emit code?
    bool seenTrue;  // has any branch in this block been active?
    bool inElse;    // are we past `else?
    std::string file{};  // where the `ifdef / `ifndef is ("" = the compiled file)
    int line{0};
    int column{0};
    int endColumn{0};
};

struct Ctx {
    MacroMap macros{};
    std::vector<CondEntry> condStack{};
    std::vector<PreprocessorError> errors{};
    // Messages from macro expansion and `define parsing, which don't know
    // the line; processSource gives them its position (pendingAt).
    std::vector<std::string> pending{};
    std::vector<std::string> includeStack{}; // cycle detection
    const std::vector<std::string>& includePaths;
    std::ostream* progressLog{nullptr}; // real-time `include` progress, see sv_preprocessor.h
    std::string output{};
    std::vector<MacroRecord> macroRecords{};
    std::vector<SourceLine> sourceMap{};
    std::vector<IncludeSite> includeSites{};
    std::vector<IncludeDirective> includeDirectives{};

    void error(PreprocessorError::Kind kind, std::string message, const std::string& file,
               int line, int column, int endColumn) {
        errors.push_back({std::move(message), file, line, column, endColumn, kind});
    }
    // The pending messages, as Other errors at `file` `line`, columns 0..`endColumn`.
    void pendingAt(const std::string& file, int line, int endColumn) {
        for (auto& m : pending)
            error(PreprocessorError::Kind::Other, std::move(m), file, line, 0, endColumn);
        pending.clear();
    }

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

// Resolves the token-paste operator (``): deletes each `` `` `` occurrence and any
// immediately adjacent horizontal whitespace, splicing the surrounding text together.
// Must run over a macro body (post parameter-substitution for function-like macros)
// before expandStr's backtick-scan sees that text, so that (a) a pasted fragment that
// forms a new macro-invocation name (e.g. `` `uvm_pack_``TYPE``(...) ``) is seen as one
// already-spliced identifier, and (b) any `` `" `` stringification markers appearing
// later in the same text are unaffected -- `` and `" are lexically distinct (two
// backticks vs. backtick+quote), so this pass can't misfire on them. Does not attempt
// to expand an adjacent nested macro invocation before pasting (matches C's `##`,
// which pastes the literal token text) -- no real SV source pastes across an
// unexpanded nested invocation, so this is not handled.
static std::string resolveTokenPaste(const std::string& src) {
    std::string result;
    result.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (src[i] == '`' && i + 1 < src.size() && src[i + 1] == '`') {
            i += 2;
            while (!result.empty() && (result.back() == ' ' || result.back() == '\t')) result.pop_back();
            while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) ++i;
            continue;
        }
        result += src[i++];
    }
    return result;
}

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
        return expandStr(resolveTokenPaste(def.body), macros, errors, depth + 1);
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
    // Step 2: resolve token-pasting, then expand any backtick macro invocations
    // in the substituted body
    return expandStr(resolveTokenPaste(substituted), macros, errors, depth + 1);
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

struct ParsedMacro {
    std::string name;
    std::string body;
    size_t      nameOffset; // offset of the name within `rest`
    bool        isFunctionLike;
    std::vector<std::string> params;
    std::vector<std::optional<std::string>> defaults;
};

// `rest` is everything after "`define". Returns {name, body} on success, nullopt on error.
static std::optional<ParsedMacro> parseMacroDefinition(std::string_view rest, MacroMap& macros,
                                                        std::vector<std::string>& errors) {
    std::string line(rest);
    size_t i = 0;
    skipSpaces(line, i);
    const size_t nameOffset = i;
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
    ParsedMacro parsed{name, def.body, nameOffset, def.isFunctionLike, def.params, def.defaults};
    macros[name] = std::move(def);
    return parsed;
}

// The doc comment of the `define on 1-based `defineLine` of `lines`
// (plan.md §6.31): the block of `//` lines and whole-line `/* */` comments
// directly above it, same rules as SvTreeWalker's declaration docs.
static std::string macroDocAbove(const std::vector<std::string>& lines, int defineLine) {
    std::vector<std::string> comments;
    for (int i = defineLine - 2; i >= 0;) {
        const std::string_view t = trimSV(lines[i]);
        if (t.substr(0, 2) == "//") {
            comments.emplace_back(t);
            --i;
            continue;
        }
        if (t.size() < 2 || t.substr(t.size() - 2) != "*/") break;
        int first = i;
        while (first >= 0 && trimSV(lines[first]).find("/*") == std::string_view::npos) --first;
        if (first < 0 || trimSV(lines[first]).substr(0, 2) != "/*") break;
        std::string block;
        for (int j = first; j <= i; ++j) block += std::string(trimSV(lines[j])) + "\n";
        block.pop_back();
        comments.push_back(block);
        i = first - 1;
    }
    std::reverse(comments.begin(), comments.end());
    return cleanDocComment(comments);
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

std::string resolveInclude(const std::string& name, const std::string& currentFile,
                           const std::vector<std::string>& includeDirs) {
    // Search list: dir of the current file, then the include dirs.
    std::vector<std::string> search;
    if (!currentFile.empty()) {
        fs::path p(currentFile);
        if (p.has_parent_path()) search.push_back(p.parent_path().string());
    }
    for (const auto& d : includeDirs) search.push_back(d);

    // An absolute name is taken as is. A relative one is never tried against
    // the working directory: the server's (or --build-db's) cwd is arbitrary,
    // and a hit there names the file relative to it, which providers later
    // absolutize against a different cwd. Normalized so `./` and `../` don't
    // give one file two names.
    if (fs::path(name).is_absolute())
        return fs::exists(name) ? fs::path(name).lexically_normal().string() : std::string{};
    for (const auto& dir : search) {
        auto candidate = fs::path(dir) / name;
        if (fs::exists(candidate)) return candidate.lexically_normal().string();
    }
    return {};
}

// `includer`/`line`: where the directive is (IncludeSite); `column` /
// `endColumn`: its extent on that line, for the errors.
static void processInclude(const std::string& filename, const std::string& currentFile,
                            const std::string& includer, int line, int column, int endColumn,
                            Ctx& ctx, int depth) {
    using Kind = PreprocessorError::Kind;
    auto fail = [&](Kind kind, std::string message) {
        ctx.error(kind, "`include: " + std::move(message), includer, line, column, endColumn);
    };
    if (depth > 16) {
        fail(Kind::IncludeDepth, "depth limit exceeded (cycle?)");
        return;
    }

    const std::string found = resolveInclude(filename, currentFile, ctx.includePaths);
    if (std::none_of(ctx.includeDirectives.begin(), ctx.includeDirectives.end(),
                     [&](const IncludeDirective& d) {
                         return d.fromFile == currentFile && d.name == filename;
                     }))
        ctx.includeDirectives.push_back({filename, currentFile, found});

    if (found.empty()) {
        fail(Kind::IncludeNotFound, "cannot find \"" + filename + "\"");
        return;
    }

    // A file may be read again while it is being read: an include guard
    // makes the second visit skip its body, as in a simulator. A third
    // entry means the second visit reached the same `include again, so
    // nothing guards it.
    if (std::count(ctx.includeStack.begin(), ctx.includeStack.end(), found) >= 2) {
        fail(Kind::IncludeCycle, "cycle detected for \"" + found + "\"");
        return;
    }

    // Stat before reading: a change after this moves the mtime away from
    // what is recorded (IncludeSite::statNs).
    const int64_t statNs = nowNs();
    const auto stat = statFile(found);
    std::ifstream f(found);
    if (!f.is_open()) {
        fail(Kind::IncludeUnreadable, "cannot open \"" + found + "\"");
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

    // Logged here -- at the moment the include is actually resolved and
    // opened, before recursing into its own content -- rather than by the
    // caller after this whole top-level `process()` call returns, so a
    // caller watching this stream sees each file as it's discovered instead
    // of all-at-once at the very end (see this method's own doc comment in
    // sv_preprocessor.h).
    if (ctx.progressLog) {
        *ctx.progressLog << "[parsed]   included: " << found << "\n";
        ctx.progressLog->flush();
    }

    // Once per file: a file read again (an include guard's second visit)
    // was read from the same disk state as far as the result goes.
    if (std::none_of(ctx.includeSites.begin(), ctx.includeSites.end(),
                     [&](const IncludeSite& s) { return s.file == found; }))
        ctx.includeSites.push_back({found, includer, line, contentHash(ss.str()),
                                    stat ? stat->size : -1, stat ? stat->mtimeNs : 0, statNs});
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
    std::vector<std::string> rawLines; // `source` split into lines, on the first `define

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
                    expandStr(std::string(code), ctx.macros, ctx.pending, 0, &shifts);
                ctx.pendingAt(mapFile, firstLineNo, static_cast<int>(line.size()));
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
        // The directive's extent: from the backtick to the end of its code.
        const int dirColumn = static_cast<int>(ws);
        const int dirEnd = [&] {
            const std::string_view code = stripLineComment(line);
            const size_t last = code.find_last_not_of(" \t\r");
            return last == std::string_view::npos ? dirColumn : static_cast<int>(last + 1);
        }();
        auto directiveError = [&](std::string message) {
            ctx.error(PreprocessorError::Kind::Other, std::move(message), mapFile, lineNo,
                      dirColumn, dirEnd);
        };

        // ---- Conditional directives (processed even when skipping) ----

        if (dir == "ifdef" || dir == "ifndef") {
            std::string macName = std::string(trimSV(stripLineComment(rest)));
            bool defined = ctx.macros.count(macName) > 0;
            bool active  = (dir == "ifdef") ? defined : !defined;
            ctx.condStack.push_back({active, active, false, mapFile, lineNo, dirColumn, dirEnd});
            emitBlank();
            continue;
        }
        if (dir == "elsif") {
            if (ctx.condStack.empty()) {
                directiveError("`elsif without `ifdef");
            } else {
                auto& top = ctx.condStack.back();
                if (top.inElse) {
                    directiveError("`elsif after `else");
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
                directiveError("`else without `ifdef");
            } else {
                auto& top = ctx.condStack.back();
                if (top.inElse) { directiveError("duplicate `else"); }
                else { top.inElse = true; top.active = !top.seenTrue; }
            }
            emitBlank();
            continue;
        }
        if (dir == "endif") {
            if (ctx.condStack.empty()) directiveError("`endif without `ifdef");
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
            auto opt = parseMacroDefinition(mergedRest, ctx.macros, ctx.pending);
            ctx.pendingAt(mapFile, defineLine, static_cast<int>(line.size()));
            if (opt) {
                const int column = static_cast<int>(i + opt->nameOffset);
                if (rawLines.empty()) {
                    std::istringstream all(source);
                    for (std::string l; std::getline(all, l);) rawLines.push_back(std::move(l));
                }
                std::string doc = macroDocAbove(rawLines, defineLine);
                if (doc.empty() && lineNo == defineLine)
                    doc = cleanDocComment({std::string(splitLineComment(mergedRest).second)});
                ctx.macroRecords.push_back({opt->name, opt->body, defineLine, column, mapFile,
                                            opt->isFunctionLike, std::move(opt->params),
                                            std::move(opt->defaults), std::move(doc)});
            }
        } else if (dir == "undef") {
            ctx.macros.erase(std::string(trimSV(stripLineComment(rest))));
            emitBlank();
        } else if (dir == "undefineall") {
            ctx.macros.clear();
            emitBlank();
        } else if (dir == "include") {
            std::string_view r = trimSV(rest);
            std::string filename;
            int nameEnd = dirEnd;  // a malformed directive: up to the end of its code
            if (!r.empty() && (r.front() == '"' || r.front() == '<')) {
                char close = r.front() == '"' ? '"' : '>';
                size_t end = r.find(close, 1);
                if (end != std::string_view::npos) {
                    filename = std::string(r.substr(1, end - 1));
                    nameEnd = static_cast<int>(r.data() - line.data() + end + 1);
                }
            }
            if (filename.empty()) {
                ctx.error(PreprocessorError::Kind::IncludeMalformed,
                          "`include: missing or malformed filename", mapFile, lineNo,
                          dirColumn, nameEnd);
                emitBlank();
            } else {
                processInclude(filename, filepath, mapFile, lineNo, dirColumn, nameEnd, ctx,
                               depth);
                // No blank line: included content replaces the `include line.
                // The recursive processSource call pushed source map entries for it.
            }
        } else {
            // Unknown directive starting the line — try macro expansion of the whole line
            int firstLineNo = lineNo;
            auto [merged, contLines] = mergeInvocationContinuation(line);
            std::vector<ColShift> shifts;
            std::string expanded = expandStr(merged, ctx.macros, ctx.pending, 0, &shifts);
            ctx.pendingAt(mapFile, firstLineNo, static_cast<int>(line.size()));
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

void SvPreprocessor::define(const MacroRecord& macro) {
    m_seeded.push_back(macro);
}

PreprocessorResult SvPreprocessor::process(const std::string& source,
                                            const std::string& filepath,
                                            std::ostream* progressLog) {
    Ctx ctx{.includePaths = m_includePaths, .progressLog = progressLog};

    // Seed macro table from predefined macros
    for (const auto& [name, value] : m_predefined)
        ctx.macros[name] = MacroDef{.isFunctionLike = false, .body = value};
    for (const auto& m : m_seeded)
        ctx.macros[m.name] = MacroDef{.isFunctionLike = m.isFunctionLike, .params = m.params,
                                      .defaults = m.defaults, .body = m.body};

    processSource(source, filepath, ctx, 0);

    // At the outermost block left open.
    if (!ctx.condStack.empty()) {
        const auto& open = ctx.condStack.front();
        ctx.error(PreprocessorError::Kind::Other, "unterminated `ifdef/`ifndef block",
                  open.file, open.line, open.column, open.endColumn);
    }

    return {std::move(ctx.output), std::move(ctx.errors), std::move(ctx.macroRecords),
            std::move(ctx.sourceMap), std::move(ctx.includeSites),
            std::move(ctx.includeDirectives)};
}
