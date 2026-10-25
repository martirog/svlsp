// The UVM slice both harness tools cut (see --slice in profile_main.cpp).
#pragma once
#include <string>

// `text` cut after the first `endclass` line at or after line `lines`, closed
// with `endpackage`; all of `text` when there is no such line.
inline std::string sliceAt(const std::string& text, size_t lines) {
    size_t pos = 0;
    for (size_t n = 1; pos < text.size(); ++n) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        if (n >= lines) {
            const auto first = text.find_first_not_of(" \t", pos);
            if (first < eol && text.compare(first, 8, "endclass") == 0)
                return text.substr(0, eol + 1) + "endpackage\n";
        }
        pos = eol + 1;
    }
    return text;
}
