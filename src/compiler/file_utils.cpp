#include "compiler/file_utils.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

std::optional<std::string> readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::optional<FileStat> statFile(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
    return FileStat{static_cast<int64_t>(st.st_size),
                    static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 + st.st_mtim.tv_nsec};
}

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
