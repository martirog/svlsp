#include "lsp/library_db_builder.h"
#include "lsp/project_manifest_parser.h"
#include "compiler/filelist_parser.h"
#include "compiler/project_config.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/project_compiler.h"
#include "db/symbol_database.h"
#include <filesystem>

LibraryDbBuilder::Result LibraryDbBuilder::build(
    const std::string& configPath, const std::string& outputPath, std::ostream* progressLog)
{
    Result result;

    ProjectConfig config;
    try {
        config = configPath.ends_with(".json")
            ? ProjectManifestParser::parse(configPath)
            : FilelistParser::parse(configPath,
                  std::filesystem::path(configPath).parent_path().string());
    } catch (const std::exception& e) {
        result.error = e.what();
        return result;
    }

    Database db(outputPath);
    db.initSchema();
    SymbolDatabase sdb(db);
    CompilationController controller(sdb, progressLog);

    result.fileCount = ProjectCompiler::loadProject(config, controller, sdb);

    if (auto stmt = db.prepare("SELECT COUNT(*) FROM diagnostics"); stmt.step())
        result.diagnosticCount = stmt.columnInt(0);

    result.ok = true;
    return result;
}
