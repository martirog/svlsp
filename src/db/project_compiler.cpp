#include "db/project_compiler.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/library_resolver.h"
#include "db/symbol_database.h"
#include "compiler/file_utils.h"
#include <algorithm>
#include <filesystem>

namespace {

// Opens each config.libraryDbs path as its own standalone, throwaway
// connection (never the live sdb's own attached-schema mechanism -- that's
// for symbol queries, not this) and merges whatever includeDirs it recorded
// at build time (SymbolDatabase::setLibraryIncludeDirs, plan.md §6.19 piece
// 4) into config.includeDirs, deduped. Deliberately doesn't call
// Database::initSchema() on the library DB -- reading must never migrate or
// otherwise mutate a file the caller may be treating as an immutable,
// archived version. Best-effort: a missing library DB file, or one that
// fails to open, is silently skipped -- config.libraryDbs pointing at
// something unreadable is reported separately (attachLibraryDbs already
// throws for that); this merge step must not turn that into a second,
// differently-worded failure.
void mergeIncludeDirsFromLibraryDbs(ProjectConfig& config) {
    for (const auto& dbPath : config.libraryDbs) {
        if (!std::filesystem::exists(dbPath)) continue;
        try {
            Database db(dbPath);
            SymbolDatabase libSdb(db);
            for (const auto& dir : libSdb.libraryIncludeDirs()) {
                if (std::find(config.includeDirs.begin(), config.includeDirs.end(), dir) ==
                    config.includeDirs.end())
                    config.includeDirs.push_back(dir);
            }
        } catch (const std::exception&) {
            // Best-effort -- see this function's own doc comment above.
        }
    }
}

} // namespace

int ProjectCompiler::loadProject(ProjectConfig& config, CompilationController& controller,
                                 SymbolDatabase& sdb) {
    sdb.attachLibraryDbs(config.libraryDbs);
    mergeIncludeDirsFromLibraryDbs(config);

    int compiledCount = 0;
    for (const auto& path : config.files) {
        if (auto text = readFile(path)) {
            controller.compile(path, *text, &config);
            ++compiledCount;
        }
    }
    compiledCount += LibraryResolver::resolve(config, controller, sdb);
    return compiledCount;
}
