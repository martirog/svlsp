#include "lsp/project_registry.h"
#include "lsp/project_manifest_parser.h"
#include "compiler/filelist_parser.h"
#include "db/project_compiler.h"
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

    ProjectConfig config = endsWith(configPath, ".json")
        ? ProjectManifestParser::parse(configPath)
        : FilelistParser::parse(configPath);

    ProjectCompiler::loadProject(config, m_controller, m_sdb);

    auto owned = std::make_unique<ProjectConfig>(std::move(config));
    const ProjectConfig* raw = owned.get();
    m_loaded[configPath] = std::move(owned);
    return raw;
}

const ProjectConfig* ProjectRegistry::configFor(const std::string& filePath) {
    std::string configPath = m_explicitConfigPath;
    if (configPath.empty()) {
        configPath = discoverConfigPath(fs::path(filePath).parent_path().string());
    }
    if (configPath.empty()) return nullptr;
    return loadAndCache(configPath);
}
