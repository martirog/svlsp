#include "lsp/library_db_builder.h"
#include "lsp/project_manifest_parser.h"
#include "compiler/filelist_parser.h"
#include "compiler/project_config.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/project_compiler.h"
#include "db/symbol_database.h"
#include <filesystem>
#include <stdexcept>

LibraryDbBuilder::Result LibraryDbBuilder::build(
    const std::string& configPath, const std::string& outputPath, std::ostream* progressLog,
    const std::string& currentVersion, bool collectDocs)
{
    Result result;
    const std::string buildVersion =
        currentVersion.empty() || collectDocs ? currentVersion
                                              : currentVersion + " (no doc comments)";

    // Wraps the whole build, not just config parsing: build() must never
    // throw (its documented contract), since a caller reached through a
    // notification handler -- resolveLibraryDbSources from
    // ProjectRegistry::loadAndCache, itself from a plain textDocument/
    // didOpen -- would have an escaping exception silently dropped by
    // lsp-framework's own dispatch (notifications get no error response to
    // report it on), not surfaced anywhere at all. Found exactly this way:
    // a live server session with a not-yet-existing cache directory (a
    // *very* real shape -- e.g. cache under a fresh "/var/cache/svlsp/")
    // threw from sqlite3_open below and vanished without a trace until
    // main.cpp's own --build-db path (which also has no try/catch of its
    // own) was checked against the same scenario.
    try {
        ProjectConfig config = configPath.ends_with(".json")
            ? ProjectManifestParser::parse(configPath)
            : FilelistParser::parse(configPath,
                  std::filesystem::path(configPath).parent_path().string());
        resolveLibraryDbSources(config, progressLog, currentVersion);

        // sqlite3_open (inside the Database constructor) does not create
        // missing parent directories -- the exact bug found live above.
        // create_directories() on an *empty* path (a bare "out.db" with no
        // directory component at all) throws rather than no-op'ing, so
        // guard for that plain-relative-path case explicitly.
        if (auto parent = std::filesystem::path(outputPath).parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);

        Database db(outputPath);
        db.initSchema();
        SymbolDatabase sdb(db);

        // A stale-cache guard, not a correctness requirement of the compile
        // pipeline itself: the per-file content-hash cache below has no way
        // to know the *parser* changed between two builds of the same
        // outputPath, only that a file's own text didn't. Force a full
        // rebuild whenever this DB's own recorded build version doesn't
        // match the binary currently running -- including "nothing
        // recorded" (a DB built before this feature, or being built into
        // outputPath for the first time). currentVersion.empty() (a caller
        // with no version information -- see build()'s own doc comment)
        // always skips this, preserving every pre-existing call site's
        // behavior exactly.
        if (!buildVersion.empty() && sdb.builtByVersion() != buildVersion)
            sdb.resetAllFiles();

        CompilationController controller(sdb, progressLog);
        controller.setCollectDocs(collectDocs);

        result.fileCount = ProjectCompiler::loadProject(config, controller, sdb);

        // Bake this build's own resolved includeDirs into the DB file
        // itself (plan.md §6.19 piece 4) -- by this point config.includeDirs
        // already reflects whatever loadProject merged in from any of this
        // config's *own* libraryDbs/libraryDbSources, so a project attaching
        // just this one DB later inherits the full transitive closure, not
        // only this config's own top-level includeDirs.
        sdb.setLibraryIncludeDirs(config.includeDirs);
        if (!buildVersion.empty())
            sdb.setBuiltByVersion(buildVersion);

        if (auto stmt = db.prepare("SELECT COUNT(*) FROM diagnostics"); stmt.step())
            result.diagnosticCount = stmt.columnInt(0);

        result.ok = true;
    } catch (const std::exception& e) {
        result = Result{};
        result.error = e.what();
    }
    return result;
}

void LibraryDbBuilder::resolveLibraryDbSources(ProjectConfig& config, std::ostream* progressLog,
                                               const std::string& currentVersion)
{
    for (const auto& src : config.libraryDbSources) {
        if (!std::filesystem::exists(src.cachePath)) {
            Result built = build(src.configPath, src.cachePath, progressLog, currentVersion);
            if (!built.ok)
                throw std::runtime_error(
                    "failed to build cached library DB '" + src.cachePath + "' from '" +
                    src.configPath + "': " + built.error);
        }
        config.libraryDbs.push_back(src.cachePath);
    }
}
