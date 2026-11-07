#include "lsp/load_order.h"
#include <algorithm>
#include <cctype>

namespace {

bool isIdStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }

void addOnce(std::vector<std::string>& v, std::string s)
{
    if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(std::move(s));
}

} // namespace

std::string baseName(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

SourceScan scanSource(const std::string& text)
{
    SourceScan out;
    std::string prev;       // the previous identifier or keyword
    bool inList = false;    // after `import` / `extends` / `implements`, until other punctuation
    bool expectRef = false; // the next identifier is a name in that list
    bool afterDirective = false; // the last token was `include
    const size_t n = text.size();
    for (size_t i = 0; i < n;) {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? n : end + 2;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            while (j < n && text[j] != '"' && text[j] != '\n') j += text[j] == '\\' ? 2 : 1;
            if (afterDirective && j <= n) addOnce(out.includes, text.substr(i + 1, j - i - 1));
            afterDirective = false;
            i = j + 1;
            continue;
        }
        if (c == '`') {
            size_t j = i + 1;
            while (j < n && isIdChar(text[j])) ++j;
            afterDirective = text.compare(i + 1, j - i - 1, "include") == 0;
            prev.clear();
            i = j;
            continue;
        }
        if (isIdStart(c)) {
            size_t j = i;
            while (j < n && isIdChar(text[j])) ++j;
            std::string word = text.substr(i, j - i);
            size_t k = j;
            while (k < n && (text[k] == ' ' || text[k] == '\t')) ++k;
            if (k + 1 < n && text[k] == ':' && text[k + 1] == ':') addOnce(out.references, word);
            if (prev == "package" || prev == "class" || prev == "module" || prev == "interface" ||
                prev == "program")
                addOnce(out.declares, word);
            else if (expectRef)
                addOnce(out.references, word);
            expectRef = false;
            if (word == "import" || word == "extends" || word == "implements")
                inList = expectRef = true;
            prev = std::move(word);
            afterDirective = false;
            i = j;
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(c))) {
            // `virtual class`, `interface class`: the keyword carries over
            // whitespace only. A comma continues an import / extends list.
            prev.clear();
            if (c == ',' && inList) expectRef = true;
            else if (c != ':') inList = expectRef = false;
            afterDirective = false;
        }
        ++i;
    }
    // Keywords that look like names after `class` / `interface` (`interface
    // class C`) are not names.
    out.declares.erase(std::remove_if(out.declares.begin(), out.declares.end(),
                                      [](const std::string& s) {
                                          return s == "class" || s == "automatic" ||
                                                 s == "static";
                                      }),
                       out.declares.end());
    return out;
}
