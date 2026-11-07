#include "lsp/library_db_builder.h"
#include "lsp/project_manifest_parser.h"
#include "lsp/symbol_resolution.h"
#include "compiler/filelist_parser.h"
#include "compiler/project_config.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/db_lock.h"
#include "db/library_check.h"
#include "db/project_refresher.h"
#include "db/schema.h"
#include "db/project_compiler.h"
#include "db/symbol_database.h"
#include <filesystem>
#include <stdexcept>

LibraryDbBuilder::Result LibraryDbBuilder::build(
    const std::string& configPath, const std::string& outputPath, std::ostream* progressLog,
    const std::string& currentVersion, bool collectDocs,
    const CompilationController::Walker& walker)
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
        resolveLibraryDbSources(config, progressLog, currentVersion, walker);

        // sqlite3_open (inside the Database constructor) does not create
        // missing parent directories -- the exact bug found live above.
        // create_directories() on an *empty* path (a bare "out.db" with no
        // directory component at all) throws rather than no-op'ing, so
        // guard for that plain-relative-path case explicitly.
        if (auto parent = std::filesystem::path(outputPath).parent_path(); !parent.empty())
            std::filesystem::create_directories(parent);

        Database db(outputPath);
        // No fsync per commit while building (plan.md §6.34 S0 F2).
        db.useWal();
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
        controller.setCalleeResolver(resolveCallees);
        controller.setTypeChecker(checkTypeRefs);
        if (walker) controller.setWalker(walker);

        result.fileCount = ProjectCompiler::loadProject(
            config, controller, sdb,
            std::filesystem::absolute(configPath).lexically_normal().string(), buildVersion,
            &result.refresh);

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

        // Leave one self-contained file (no -wal beside it), so it can be
        // attached read-only, copied or archived as a library DB. A reader
        // open on it keeps WAL; that is harmless.
        try {
            db.execute("PRAGMA journal_mode = DELETE");
        } catch (const std::exception&) {
        }

        result.ok = true;
    } catch (const std::exception& e) {
        result = Result{};
        result.error = e.what();
    }
    return result;
}

void LibraryDbBuilder::resolveLibraryDbSources(ProjectConfig& config, std::ostream* progressLog,
                                               const std::string& currentVersion,
                                               const CompilationController::Walker& walker,
                                               std::vector<std::string>* rebuilt)
{
    namespace fs = std::filesystem;
    for (const auto& src : config.libraryDbSources) {
        if (!fs::exists(src.cachePath)) {
            Result built = build(src.configPath, src.cachePath, progressLog, currentVersion,
                                 /*collectDocs=*/true, walker);
            if (!built.ok)
                throw std::runtime_error(
                    "failed to build cached library DB '" + src.cachePath + "' from '" +
                    src.configPath + "': " + built.error);
        } else if (const LibraryStatus status = checkLibraryDb(src.cachePath); status.stale()) {
            // svlsp owns this cache: bring it up to date (plan.md §6.34 S7),
            // from a copy, so readers of the old file are never disturbed.
            DbLock lock;
            lock.acquire(src.cachePath, "build-db", /*wait=*/true);
            const std::string tmp = src.cachePath + ".tmp";
            fs::copy_file(src.cachePath, tmp, fs::copy_options::overwrite_existing);
            Result built = build(src.configPath, tmp, progressLog, currentVersion,
                                 /*collectDocs=*/true, walker);
            if (!built.ok) {
                fs::remove(tmp);
                throw std::runtime_error("failed to rebuild stale library DB '" + src.cachePath +
                                         "' (" + status.describe() + "): " + built.error);
            }
            fs::rename(tmp, src.cachePath);
            if (rebuilt) rebuilt->push_back(src.cachePath);
        }
        config.libraryDbs.push_back(src.cachePath);
    }
}

LibraryDbBuilder::CheckResult LibraryDbBuilder::check(const std::string& configPath,
                                                      const std::string& outputPath,
                                                      const std::string& currentVersion,
                                                      bool collectDocs)
{
    CheckResult r;
    const std::string buildVersion =
        currentVersion.empty() || collectDocs ? currentVersion
                                              : currentVersion + " (no doc comments)";
    try {
        ProjectConfig config = configPath.ends_with(".json")
            ? ProjectManifestParser::parse(configPath)
            : FilelistParser::parse(configPath,
                  std::filesystem::path(configPath).parent_path().string());
        r.ok = true;
        if (!std::filesystem::exists(outputPath)) {
            r.stale = true;
            r.why = "'" + outputPath + "' doesn't exist";
            return r;
        }
        // Libraries it is built on first: a stale one makes it stale too.
        for (const auto& src : config.libraryDbSources) {
            if (!std::filesystem::exists(src.cachePath)) {
                r.stale = true;
                r.why = "library '" + src.cachePath + "': not built";
                return r;
            }
            if (const LibraryStatus st = checkLibraryDb(src.cachePath); st.stale()) {
                r.stale = true;
                r.why = "library '" + src.cachePath + "': " + st.describe();
                return r;
            }
            config.libraryDbs.push_back(src.cachePath);
        }
        for (const auto& lib : config.libraryDbs) {
            if (const LibraryStatus st = checkLibraryDb(lib); st.stale()) {
                r.stale = true;
                r.why = "library '" + lib + "': " + st.describe();
                return r;
            }
        }
        Database db(outputPath);
        if (db.schemaVersion() != db::SCHEMA_VERSION) {
            r.stale = true;
            r.why = "schema v" + std::to_string(db.schemaVersion()) + ", this svlsp writes v" +
                    std::to_string(db::SCHEMA_VERSION);
            return r;
        }
        SymbolDatabase sdb(db);
        if (!buildVersion.empty() && sdb.builtByVersion() != buildVersion) {
            r.stale = true;
            r.why = "built by svlsp " + sdb.builtByVersion();
            return r;
        }
        CompilationController controller(sdb);
        controller.setCollectDocs(collectDocs);
        controller.setCalleeResolver(resolveCallees);
        controller.setTypeChecker(checkTypeRefs);
        ProjectCompiler::attachLibraries(config, sdb);
        // The plan records new stat data; roll it back.
        Database::Transaction txn(db, "dry_run");
        const auto plan = ProjectRefresher::plan(
            std::filesystem::absolute(configPath).lexically_normal().string(), config,
            controller, sdb);
        if (plan.configChanged) {
            r.stale = true;
            r.why = "the config's settings changed";
        } else if (!plan.compile.empty() || !plan.remove.empty()) {
            r.stale = true;
            r.why = std::to_string(plan.compile.size()) + " units to compile, " +
                    std::to_string(plan.remove.size()) + " files to remove";
            if (!plan.compile.empty()) r.why += ", e.g. " + plan.compile.front();
            else r.why += ", e.g. " + plan.remove.front();
        }
    } catch (const std::exception& e) {
        r = CheckResult{};
        r.why = e.what();
    }
    return r;
}
