#pragma once
#include "compiler/project_config.h"
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <vector>

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

    // Deferred loading (plan.md §6.34 S4): from now on, configFor() only
    // parses a newly discovered config and hands its path to `onNew` (called
    // with whatever lock the caller of configFor holds); building its library
    // DBs and compiling it is the caller's, through mutableConfig().
    using NewProjectCallback = std::function<void(const std::string& configPath)>;
    void deferLoading(NewProjectCallback onNew) { m_onNew = std::move(onNew); }

    // The config discovered for `filePath` ("" = none), without loading it.
    std::string configPathFor(const std::string& filePath) const;

    // The loaded config at `configPath`, or nullptr.
    ProjectConfig* mutableConfig(const std::string& configPath);

    // Parses the config at `configPath` (by its extension, as discovery
    // does). Throws on a malformed one.
    static ProjectConfig parseConfig(const std::string& configPath);

    // The DB a project gets when its config names none (plan.md §6.34 S3b,
    // D1): $XDG_CACHE_HOME/svlsp/<SHA-256 of the config's absolute path,
    // first 16 hex digits>.db, with ~/.cache when the variable is unset.
    // Two checkouts of one repo get two DBs. "" when neither is set.
    static std::string defaultDatabasePath(const std::string& configPath);

    // Every config loaded so far.
    std::vector<std::string> configPaths() const;

private:
    CompilationController& m_controller;
    SymbolDatabase&         m_sdb;
    std::string             m_explicitConfigPath;
    std::map<std::string, std::unique_ptr<ProjectConfig>> m_loaded;
    NewProjectCallback      m_onNew;

    static std::string discoverConfigPath(const std::string& fileDir);
    const ProjectConfig* loadAndCache(const std::string& configPath);
};
