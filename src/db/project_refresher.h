#pragma once
#include "compiler/project_config.h"
#include "db/compilation_controller.h"
#include "db/symbol_database.h"
#include <string>
#include <unordered_map>
#include <vector>

class CompilationController;
class SymbolDatabase;

// Brings a project's rows in the DB up to date with the files on disk
// (plan.md §6.34 S2): recompiles only the units whose inputs changed,
// compiles what is new, deletes what is gone. A first load (nothing
// stored), a `--build-db` rerun and a branch switch are the same operation.
//
// plan() decides; apply() does it. Neither holds state between calls: the
// project's units, its config fingerprint and each file's identity are in
// the DB (project_info, project_units, files).
class ProjectRefresher {
public:
    struct Plan {
        std::string projectKey;            // the config's absolute path ("" = none)
        std::vector<std::string> compile;  // units to compile, in order
        std::vector<std::string> origins;  // compile[i]'s: 'config' or 'library'
        std::vector<std::string> remove;   // files whose rows go (gone, or dropped)
        std::vector<std::string> force;    // units compiled with forceRecompile
        int checkedFiles{0};               // files stat'ed
        int rehashedFiles{0};              // of those, read and hashed
        bool configChanged{false};         // the config fingerprint differs
        std::string svlspVersion{};        // recorded in project_info ("" = unknown)
    };

    struct Result {
        int compiled{0};                   // units compiled (also library-resolved)
        int removed{0};                    // files removed
        bool rechecked{false};             // the cross-file checks re-ran (D3)
        double recheckMs{0};               // how long that took
        // Files whose diagnostics differ from before the refresh (to publish).
        std::vector<std::string> changedDiagnostics;
    };

    // Compares the DB's record of project `projectKey` with `config` and the
    // files on disk. Units: the config's files and the ones library
    // resolution added last time. One is compiled when it is new, or when
    // CompilationController::isUpToDate says it would miss the cache. A unit
    // the config no longer lists, or a file that is gone from disk, is
    // removed; whoever included it recompiles. Stat data of files whose
    // content turned out unchanged is updated here (the only write), so the
    // next plan needn't read them again.
    static Plan plan(const std::string& projectKey, const ProjectConfig& config,
                     CompilationController& controller, SymbolDatabase& sdb);

    // Carries out `plan`: removes, compiles in order, re-runs library
    // resolution (a new -y file can resolve an instantiation), drops library
    // units nothing instantiates any more, and -- when declarations were
    // added or removed -- re-runs the cross-file checks those can change, in
    // every unit, from their stored inputs (D3). Records the units and the
    // config fingerprint.
    static Result apply(const Plan& plan, const ProjectConfig& config,
                        CompilationController& controller, SymbolDatabase& sdb);

    // apply() in steps, for a caller that runs each unit's compile itself
    // (the server's background queue, plan.md §6.34 S4): begin() snapshots
    // the diagnostics and does the removals; after each unit of
    // `plan.compile` is compiled, unitCompiled() records it, given the
    // unit's SymbolDatabase::declarationsOfUnit from just before its compile;
    // finish() does the rest of apply(). apply() is exactly that.
    struct Progress {
        std::unordered_map<std::string, std::string> diagnosticsBefore;
        std::vector<std::string> sited;       // files whose include sites changed
        CompilationController::RecheckNames names; // declared names added / removed
        Result result;
    };
    static Progress begin(const Plan& plan, SymbolDatabase& sdb);
    static void unitCompiled(Progress& progress, const Plan& plan, const std::string& unit,
                             const std::vector<SymbolDatabase::Declaration>& before,
                             SymbolDatabase& sdb);
    static Result finish(Progress&& progress, const Plan& plan, const ProjectConfig& config,
                         CompilationController& controller, SymbolDatabase& sdb);

    static Result refresh(const std::string& projectKey, const ProjectConfig& config,
                          CompilationController& controller, SymbolDatabase& sdb,
                          const std::string& svlspVersion = "")
    {
        auto p = plan(projectKey, config, controller, sdb);
        p.svlspVersion = svlspVersion;
        return apply(p, config, controller, sdb);
    }
};
