#pragma once
#include "compiler/project_config.h"
#include <map>
#include <memory>
#include <string>

class CompilationController;
class SymbolDatabase;

// Discovers, loads, and caches the ProjectConfig applicable to a given
// opened file.
//
// Discovery order per file (first match wins):
//   1. An explicit config path set via setExplicitConfigPath() -- always
//      wins, no upward search performed, regardless of the file's location.
//   2. Upward directory search starting at the file's own directory,
//      checking (in this precedence, at each directory level, before
//      moving to the parent): .svlsp.json, svlsp.json, .svlsp.f, svlsp.f,
//      files.f.
//   3. Nothing found by the time the filesystem root is reached ->
//      nullptr (today's single-file behavior, unchanged).
//
// Why upward-search-from-file instead of keying off the client's rootUri:
// the Emacs test harness (and many editors) resolve workspace root via VCS
// (".git") detection, which for a repo containing multiple independent
// sub-projects would report the whole repo as root -- unreliable for a
// nested-project layout. Upward search (the same approach tsconfig.json/
// Cargo.toml use) sidesteps that; an explicit initializationOptions path
// remains available as a fully deterministic override.
//
// Results are cached keyed by the discovered config file's own path (which
// is effectively a 1:1 stand-in for "project root" here): ProjectCompiler::
// loadProject runs exactly once per config file, on the first file touched
// under it.
//
// Known limitation, explicitly out of scope: no file-watching exists in
// this codebase, so editing a manifest/filelist after discovery isn't
// picked up until server restart.
class ProjectRegistry {
public:
    ProjectRegistry(CompilationController& controller, SymbolDatabase& sdb);

    void setExplicitConfigPath(std::string path);

    // Returns the ProjectConfig applicable to `filePath`, lazily discovering
    // and loading it (running ProjectCompiler::loadProject on first touch)
    // via the discovery order above. Returns nullptr if none applies.
    const ProjectConfig* configFor(const std::string& filePath);

private:
    CompilationController& m_controller;
    SymbolDatabase&         m_sdb;
    std::string             m_explicitConfigPath;
    std::map<std::string, std::unique_ptr<ProjectConfig>> m_loaded;

    static std::string discoverConfigPath(const std::string& fileDir);
    const ProjectConfig* loadAndCache(const std::string& configPath);
};
