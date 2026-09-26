#include "lsp/macro_resolution.h"
#include "lsp/symbol_utils.h"
#include <array>
#include <cctype>
#include <string_view>

namespace {

bool isIdentChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// IEEE 1800-2017 Clause 22 directive keywords: a backtick followed by one
// of these is the directive, not a macro use.
constexpr std::array<std::string_view, 22> kDirectives = {
    "begin_keywords", "celldefine", "default_nettype", "define", "else", "elsif",
    "end_keywords", "endcelldefine", "endif", "ifdef", "ifndef", "include", "line",
    "nounconnected_drive", "pragma", "resetall", "timescale", "unconnected_drive", "undef",
    "undefineall", "__FILE__", "__LINE__"};

bool isDirective(std::string_view word)
{
    for (auto d : kDirectives)
        if (d == word) return true;
    return false;
}

// Directives whose first operand is a macro name.
bool takesMacroName(std::string_view word)
{
    return word == "define" || word == "undef" || word == "ifdef" || word == "ifndef" ||
           word == "elsif";
}

std::optional<size_t> offsetOf(const std::string& text, unsigned line, unsigned character)
{
    size_t offset = 0;
    for (unsigned l = 0; l < line; ++l) {
        size_t nl = text.find('\n', offset);
        if (nl == std::string::npos) return std::nullopt;
        offset = nl + 1;
    }
    offset += character;
    return offset <= text.size() ? std::optional<size_t>(offset) : std::nullopt;
}

} // namespace

bool isMacroOccurrence(const std::string& text, size_t offset)
{
    if (offset >= text.size() || !isIdentChar(text[offset])) return false;
    if (offset > 0 && isIdentChar(text[offset - 1])) return false; // not a word start
    size_t end = offset;
    while (end < text.size() && isIdentChar(text[end])) ++end;
    const std::string_view word(text.data() + offset, end - offset);

    if (offset > 0 && text[offset - 1] == '`') return !isDirective(word);

    // The first operand of a name-taking directive at the start of the line.
    size_t i = text.rfind('\n', offset);
    i = i == std::string::npos ? 0 : i + 1;
    while (i < offset && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= offset || text[i] != '`') return false;
    const size_t dirStart = ++i;
    while (i < offset && isIdentChar(text[i])) ++i;
    if (!takesMacroName(std::string_view(text.data() + dirStart, i - dirStart))) return false;
    while (i < offset && (text[i] == ' ' || text[i] == '\t')) ++i;
    return i == offset;
}

std::optional<MacroNameAt> macroNameAt(const std::string& text, unsigned line,
                                       unsigned character)
{
    auto offset = offsetOf(text, line, character);
    if (!offset) return std::nullopt;
    const std::string blanked = blankCommentsAndStrings(text);

    size_t start = *offset;
    if (start < blanked.size() && blanked[start] == '`') {
        ++start; // on the backtick: the name after it
    } else {
        if (start >= blanked.size() || !isIdentChar(blanked[start])) return std::nullopt;
        while (start > 0 && isIdentChar(blanked[start - 1])) --start;
    }
    if (!isMacroOccurrence(blanked, start)) return std::nullopt;

    size_t end = start;
    while (end < blanked.size() && isIdentChar(blanked[end])) ++end;
    const lsp::Position pos = positionForOffset(text, start);
    return MacroNameAt{text.substr(start, end - start), pos.line, pos.character};
}

std::optional<MacroRow> pickMacro(const std::vector<MacroRow>& rows, const std::string& curPath,
                                  int line1)
{
    std::optional<MacroRow> before, other, after;
    for (const auto& r : rows) {
        if (r.filePath == curPath) {
            if (r.line <= line1) before = r;
            else if (!after) after = r;
        } else if (!other) {
            other = r;
        }
    }
    return before ? before : other ? other : after;
}

std::string macroSignature(const MacroRow& macro)
{
    std::string sig = "`" + macro.name;
    if (!macro.isFunctionLike) return sig;
    sig += "(";
    for (size_t i = 0; i < macro.params.size(); ++i) {
        if (i > 0) sig += ", ";
        sig += macro.params[i];
        if (i < macro.defaults.size() && macro.defaults[i]) sig += " = " + *macro.defaults[i];
    }
    return sig + ")";
}
