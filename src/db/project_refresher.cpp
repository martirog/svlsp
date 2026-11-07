#include "db/project_refresher.h"
#include "db/compilation_controller.h"
#include "db/database.h"
#include "db/library_resolver.h"
#include "db/symbol_database.h"
#include "compiler/content_hash.h"
#include "compiler/file_utils.h"
#include <algorithm>
#include <iterator>
#include <chrono>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace {

bool contains(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

using Declarations = std::vector<SymbolDatabase::Declaration>;

std::string scopeTail(const std::string& scope)
{
    const auto sep = scope.rfind("::");
    return sep == std::string::npos ? scope : scope.substr(sep + 2);
}

// Declarations that went away: each name, and its scope's last segment (a
// function that lost a port, a class that lost a member).
void addChanged(CompilationController::RecheckNames& names, const Declarations& gone)
{
    for (const auto& d : gone) {
        names.changed.insert(d.name);
        if (!d.scope.empty()) names.changed.insert(scopeTail(d.scope));
    }
}

// Declarations that appeared: each name; a port also changes its function.
void addAdded(CompilationController::RecheckNames& names, const Declarations& added)
{
    for (const auto& d : added) {
        names.added.insert(d.name);
        if (d.kind == "Port" && !d.scope.empty()) names.changed.insert(scopeTail(d.scope));
    }
}

// What a recompile changed: what `after` declares that `before` didn't, and
// the other way round.
void diffNames(CompilationController::RecheckNames& names, const Declarations& before,
               const Declarations& after)
{
    Declarations added, removed;
    std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
                        std::back_inserter(added));
    std::set_difference(before.begin(), before.end(), after.begin(), after.end(),
                        std::back_inserter(removed));
    addAdded(names, added);
    addChanged(names, removed);
}

} // namespace

ProjectRefresher::Plan ProjectRefresher::plan(const std::string& projectKey,
                                              const ProjectConfig& config,
                                              CompilationController& controller,
                                              SymbolDatabase& sdb, bool rehash)
{
    Plan p;
    p.projectKey = projectKey;
    const auto info = sdb.projectInfo(projectKey);
    p.configChanged = info && info->configFp != controller.configFingerprint(&config);

    const auto stored = sdb.projectUnits(projectKey);
    std::unordered_map<std::string, std::string> storedOrigin(stored.begin(), stored.end());

    // Every file the project's units are made of: stat each, and read the
    // ones whose stat data changed (or was taken racily). Same content: just
    // record the new stat data. Gone: it is removed below.
    std::set<std::string> scan(config.files.begin(), config.files.end());
    for (const auto& [unit, origin] : stored) {
        scan.insert(unit);
        for (auto& inc : sdb.includedFilesOf(unit)) scan.insert(std::move(inc));
    }
    std::unordered_set<std::string> gone;
    for (const auto& file : scan) {
        const auto known = sdb.storedFile(file);
        const int64_t statNs = nowNs();
        const auto st = statFile(file);
        ++p.checkedFiles;
        if (!known) continue;
        if (!st) {
            gone.insert(file);
            continue;
        }
        const bool statSame = known->size == st->size && known->mtimeNs == st->mtimeNs;
        if (!rehash && statSame && known->mtimeNs + kRacyWindowNs < known->statNs) continue;
        ++p.rehashedFiles;
        const auto text = readFile(file);
        if (!text) {
            gone.insert(file);
            continue;
        }
        if (contentHash(*text) == known->hash)
            sdb.setFileIdentity(sdb.fileIdFor(file),
                                {known->hash, st->size, st->mtimeNs, statNs, known->inputsFp});
        else if (statSame)
            // Changed behind unchanged stat data (what --rehash is for): drop
            // the stat data, so the cache check reads the file too.
            sdb.setFileIdentity(sdb.fileIdFor(file), {known->hash, -1, 0, 0, known->inputsFp});
    }

    auto schedule = [&](const std::string& path, const std::string& origin) {
        if (contains(p.compile, path)) return;
        p.compile.push_back(path);
        p.origins.push_back(origin);
    };

    // The config's files, in order: new ones, and those whose inputs changed.
    std::unordered_set<std::string> listed;
    for (const auto& file : config.files) {
        if (!listed.insert(file).second || gone.count(file) || !statFile(file)) continue;
        if (p.configChanged || !storedOrigin.count(file) || !controller.isUpToDate(file, &config))
            schedule(file, "config");
    }

    // Units of the last refresh: dropped from the config, gone, or (library
    // ones) changed.
    for (const auto& [unit, origin] : stored) {
        if (listed.count(unit)) continue;
        if (origin == "config" || gone.count(unit)) {
            p.remove.push_back(unit);
            continue;
        }
        if (p.configChanged || !controller.isUpToDate(unit, &config)) schedule(unit, "library");
    }

    // Other files that are gone (a unit's includes); a unit's would be too.
    for (const auto& file : gone)
        if (!contains(p.remove, file)) p.remove.push_back(file);
    std::sort(p.remove.begin(), p.remove.end());

    // Whoever included a removed file compiles again, even if what it read
    // can't tell: the file may still be on disk (dropped from the config,
    // now only `include'd).
    for (const auto& file : p.remove)
        for (const auto& includer : sdb.includersOf(file)) {
            if (contains(p.remove, includer)) continue;
            const auto it = storedOrigin.find(includer);
            if (it == storedOrigin.end() && !listed.count(includer)) continue;
            schedule(includer, it == storedOrigin.end() ? "config" : it->second);
            if (!contains(p.force, includer)) p.force.push_back(includer);
        }
    return p;
}

ProjectRefresher::Progress ProjectRefresher::begin(const Plan& plan, SymbolDatabase& sdb)
{
    Progress pr;
    pr.diagnosticsBefore = sdb.diagnosticsDigest();
    const std::string& key = plan.projectKey;
    Database::Transaction txn(sdb.database(), "refresh_begin");

    // Removed files first, so what they declared no longer satisfies the
    // units compiled next. Files they included get their "included in N
    // scopes" row recomputed at the end.
    for (const auto& file : plan.remove) {
        for (const auto& site : sdb.includeSitesOfUnit(file)) pr.sited.push_back(site.file);
        const int64_t fid = sdb.fileIdFor(file);
        if (fid >= 0 && sdb.isUnitOfOtherProject(key, fid) && statFile(file)) {
            sdb.removeProjectUnit(key, fid);
            continue;
        }
        addChanged(pr.names, sdb.declarationsOfUnit(file));
        if (sdb.removeFile(file)) ++pr.result.removed;
    }
    txn.commit();
    return pr;
}

void ProjectRefresher::unitCompiled(Progress& pr, const Plan& plan, const std::string& unit,
                                    const Declarations& before, SymbolDatabase& sdb)
{
    const auto it = std::find(plan.compile.begin(), plan.compile.end(), unit);
    const int64_t fid = sdb.fileIdFor(unit);
    if (it == plan.compile.end() || fid < 0) return;
    // A new unit only adds names: a call it could change was unresolved.
    const auto after = sdb.declarationsOfUnit(unit);
    if (before.empty())
        for (const auto& d : after) pr.names.added.insert(d.name);
    else
        diffNames(pr.names, before, after);
    sdb.addProjectUnit(plan.projectKey, fid, plan.origins[it - plan.compile.begin()]);
    ++pr.result.compiled;
}

ProjectRefresher::Result ProjectRefresher::finish(Progress&& pr, const Plan& plan,
                                                  const ProjectConfig& config,
                                                  CompilationController& controller,
                                                  SymbolDatabase& sdb)
{
    const std::string& key = plan.projectKey;
    Result r = std::move(pr.result);
    Database::Transaction txn(sdb.database(), "refresh_finish");

    // Library resolution, after the recompiles: a new -y file can resolve an
    // instantiation, a changed one can stop resolving one.
    std::vector<std::string> resolved;
    r.compiled += LibraryResolver::resolve(config, controller, sdb, &resolved);
    for (const auto& file : resolved) {
        sdb.addProjectUnit(key, sdb.fileIdFor(file), "library");
        for (const auto& d : sdb.declarationsOfUnit(file)) pr.names.added.insert(d.name);
    }
    // Library units nothing instantiates any more go (a fresh build wouldn't
    // have them); dropping one can make another unneeded.
    for (bool dropped = true; dropped;) {
        dropped = false;
        for (const auto& [unit, origin] : sdb.projectUnits(key)) {
            const int64_t fid = sdb.fileIdFor(unit);
            if (origin != "library" || sdb.declaresInstantiatedDesign(fid) ||
                sdb.isUnitOfOtherProject(key, fid))
                continue;
            for (const auto& site : sdb.includeSitesOfUnit(unit)) pr.sited.push_back(site.file);
            addChanged(pr.names, sdb.declarationsOfUnit(unit));
            sdb.removeFile(unit);
            ++r.removed;
            dropped = true;
        }
    }

    // The cross-file checks of every unit that what was declared or removed
    // can change.
    if (!pr.names.empty()) {
        const auto start = std::chrono::steady_clock::now();
        controller.recheckUnits(sdb.unitsWithChecks(), pr.names);
        r.rechecked = true;
        r.recheckMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start).count();
    }
    std::sort(pr.sited.begin(), pr.sited.end());
    pr.sited.erase(std::unique(pr.sited.begin(), pr.sited.end()), pr.sited.end());
    controller.refreshContextDiagnostics(pr.sited);

    sdb.setProjectInfo(key, {controller.configFingerprint(&config), plan.svlspVersion});
    txn.commit();

    const auto after = sdb.diagnosticsDigest();
    for (const auto& [path, digest] : after) {
        const auto it = pr.diagnosticsBefore.find(path);
        if (it == pr.diagnosticsBefore.end() || it->second != digest)
            r.changedDiagnostics.push_back(path);
    }
    for (const auto& [path, digest] : pr.diagnosticsBefore)
        if (!after.count(path)) r.changedDiagnostics.push_back(path);
    std::sort(r.changedDiagnostics.begin(), r.changedDiagnostics.end());
    return r;
}

ProjectRefresher::Result ProjectRefresher::apply(const Plan& plan, const ProjectConfig& config,
                                                 CompilationController& controller,
                                                 SymbolDatabase& sdb)
{
    auto pr = begin(plan, sdb);
    for (size_t i = 0; i < plan.compile.size(); ++i) {
        const std::string& unit = plan.compile[i];
        const auto text = readFile(unit);
        if (!text) continue;
        const auto before = sdb.declarationsOfUnit(unit);
        controller.compile(unit, *text, &config, nullptr, contains(plan.force, unit));
        unitCompiled(pr, plan, unit, before, sdb);
    }
    return finish(std::move(pr), plan, config, controller, sdb);
}
