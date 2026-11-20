#include "compiler/path_names.h"
#include <filesystem>
#include <map>
#include <mutex>

namespace fs = std::filesystem;

namespace pathnames {

namespace {

std::mutex                         g_mutex;
std::map<std::string, std::string> g_files; // canonical file -> client name
std::map<std::string, std::string> g_dirs;  // canonical dir -> client name

} // namespace

std::string canonical(const std::string& path)
{
    if (path.empty()) return path;
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) abs = fs::path(path);
    fs::path c = fs::weakly_canonical(abs, ec);
    if (ec) return abs.lexically_normal().string();
    std::string s = c.string();
    // weakly_canonical keeps a trailing separator from its input.
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string fromClient(const std::string& clientPath)
{
    const std::string canon = canonical(clientPath);
    std::error_code ec;
    const fs::path client = fs::absolute(clientPath, ec).lexically_normal();
    if (ec || client.string() == canon) return canon;

    std::lock_guard lock{g_mutex};
    g_files[canon] = client.string();
    // Each directory above it whose canonical name differs, up to the
    // first that doesn't (a symlink higher up shows in every one below).
    for (fs::path dir = client.parent_path(); dir.has_relative_path(); dir = dir.parent_path()) {
        const std::string canonDir = canonical(dir.string());
        if (canonDir == dir.string()) break;
        g_dirs[canonDir] = dir.string();
    }
    return canon;
}

std::string toClient(const std::string& path)
{
    std::lock_guard lock{g_mutex};
    if (g_files.empty()) return path;
    if (const auto it = g_files.find(path); it != g_files.end()) return it->second;
    const fs::path p(path);
    for (fs::path dir = p.parent_path(); dir.has_relative_path(); dir = dir.parent_path()) {
        if (const auto it = g_dirs.find(dir.string()); it != g_dirs.end())
            return (fs::path(it->second) / p.lexically_relative(dir)).string();
    }
    return path;
}

void clearClientNames()
{
    std::lock_guard lock{g_mutex};
    g_files.clear();
    g_dirs.clear();
}

} // namespace pathnames
