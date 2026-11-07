#include "lsp/project_registry.h"
#include "lsp/project_manifest_parser.h"
#include "lsp/library_db_builder.h"
#include "compiler/filelist_parser.h"
#include "db/project_compiler.h"
#include "compiler/content_hash.h"
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

ProjectRegistry::ProjectRegistry(CompilationController& controller, SymbolDatabase& sdb)
    : m_controller(controller)
    , m_sdb(sdb)
{}

void ProjectRegistry::setExplicitConfigPath(std::string path) {
    m_explicitConfigPath = std::move(path);
}

std::string ProjectRegistry::discoverConfigPath(const std::string& fileDir) {
    static constexpr const char* kCandidates[] = {
        ".svlsp.json", "svlsp.json", ".svlsp.f", "svlsp.f", "files.f",
    };

    fs::path dir = fs::path(fileDir);
    while (true) {
        for (const char* name : kCandidates) {
            fs::path candidate = dir / name;
            if (fs::exists(candidate)) return candidate.lexically_normal().string();
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break; // reached the filesystem root
        dir = parent;
    }
    return "";
}

const ProjectConfig* ProjectRegistry::loadAndCache(const std::string& configPath) {
    if (auto it = m_loaded.find(configPath); it != m_loaded.end()) return it->second.get();

    // FilelistParser defaults bare relative paths to the process's CWD
    // (matching CLI-tool semantics), which has no relation to a discovered
    // project file's location -- pass its own directory explicitly.
    // ProjectManifestParser already resolves against the manifest's own
    // directory internally, so it needs no such override.
    ProjectConfig config = parseConfig(configPath);

    if (m_onNew) {
        auto owned = std::make_unique<ProjectConfig>(std::move(config));
        const ProjectConfig* raw = owned.get();
        m_loaded[configPath] = std::move(owned);
        m_onNew(configPath);
        return raw;
    }

    // Build-and-cache any lazily-referenced library DBs (plan.md §6.19
    // piece 3) before compiling -- turns each libraryDbSources entry into
    // an ordinary libraryDbs one, building it first only if its cachePath
    // doesn't already exist.
    // The server's parse (its worker, plan.md §6.33 C1) builds them too.
    LibraryDbBuilder::resolveLibraryDbSources(config, nullptr, "", m_controller.walker());

    ProjectCompiler::loadProject(config, m_controller, m_sdb,
                                 fs::absolute(configPath).lexically_normal().string());

    auto owned = std::make_unique<ProjectConfig>(std::move(config));
    const ProjectConfig* raw = owned.get();
    m_loaded[configPath] = std::move(owned);
    return raw;
}

ProjectConfig ProjectRegistry::parseConfig(const std::string& configPath) {
    return endsWith(configPath, ".json")
        ? ProjectManifestParser::parse(configPath)
        : FilelistParser::parse(configPath, fs::path(configPath).parent_path().string());
}

std::string ProjectRegistry::defaultDatabasePath(const std::string& configPath) {
    std::string base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) base = xdg;
    else if (const char* home = std::getenv("HOME"); home && *home) base = std::string(home) + "/.cache";
    else return "";
    const std::string key = fs::absolute(configPath).lexically_normal().string();
    return base + "/svlsp/" + contentHash(key).substr(0, 16) + ".db";
}

std::string ProjectRegistry::configPathFor(const std::string& filePath) const {
    if (!m_explicitConfigPath.empty()) return m_explicitConfigPath;
    return discoverConfigPath(fs::path(filePath).parent_path().string());
}

const ProjectConfig* ProjectRegistry::configFor(const std::string& filePath) {
    const std::string configPath = configPathFor(filePath);
    if (configPath.empty()) return nullptr;
    return loadAndCache(configPath);
}

ProjectConfig* ProjectRegistry::mutableConfig(const std::string& configPath) {
    const auto it = m_loaded.find(configPath);
    return it == m_loaded.end() ? nullptr : it->second.get();
}

std::vector<std::string> ProjectRegistry::configPaths() const {
    std::vector<std::string> paths;
    for (const auto& [path, config] : m_loaded) paths.push_back(path);
    return paths;
}
