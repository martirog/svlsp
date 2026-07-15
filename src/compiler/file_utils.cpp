#include "compiler/file_utils.h"
#include <fstream>
#include <sstream>

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
