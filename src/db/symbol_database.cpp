#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <algorithm>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <optional>
#include <set>
#include <ctime>

// Stringify ParseRecordKind for storage.
static std::string kindStr(ParseRecordKind k)
{
    switch (k) {
    case ParseRecordKind::Module:    return "Module";
    case ParseRecordKind::Interface: return "Interface";
    case ParseRecordKind::Package:   return "Package";
    case ParseRecordKind::Class:     return "Class";
    case ParseRecordKind::Function:  return "Function";
    case ParseRecordKind::Task:      return "Task";
    case ParseRecordKind::Port:      return "Port";
    case ParseRecordKind::Signal:    return "Signal";
    case ParseRecordKind::Parameter: return "Parameter";
    case ParseRecordKind::Macro:     return "Macro";
    case ParseRecordKind::Program:   return "Program";
    case ParseRecordKind::Typedef:     return "Typedef";
    case ParseRecordKind::EnumLiteral: return "EnumLiteral";
    case ParseRecordKind::Member:      return "Member";
    case ParseRecordKind::Genvar:      return "Genvar";
    case ParseRecordKind::Block:       return "Block";
    case ParseRecordKind::Instance:    return "Instance";
    }
    return "Unknown";
}

// Shared column projection used by all symbol queries below.
// Columns: 0=id 1=kind 2=name 3=line 4=col 5=parent 6=detail 7=end_line 8=scope 9=path
static SymbolRow rowFromStmt(const Database::Statement& s)
{
    return {s.columnInt(0),
            s.columnText(1),
            s.columnText(2),
            static_cast<int>(s.columnInt(3)),
            static_cast<int>(s.columnInt(4)),
            s.columnText(5),
            s.columnText(6),
            s.columnText(9),
            static_cast<int>(s.columnInt(7)),
            s.columnText(8)};
}

SymbolDatabase::SymbolDatabase(Database& db)
    : m_db{db}
{}

void SymbolDatabase::attachLibraryDbs(const std::vector<std::string>& paths)
{
    for (const auto& path : paths) {
        if (std::find(m_attachedLibraryPaths.begin(), m_attachedLibraryPaths.end(), path)
            != m_attachedLibraryPaths.end())
            continue;
        std::string alias = "lib" + std::to_string(m_attachedLibraryPaths.size());
        m_db.prepare("ATTACH DATABASE ? AS " + alias).bind(1, path).step();
        m_attachedLibraryPaths.push_back(path);
        clearIncludeCaches();
    }
}

void SymbolDatabase::setLibraryIncludeDirs(const std::vector<std::string>& dirs)
{
    m_db.execute("DELETE FROM library_include_dirs");
    int ordinal = 0;
    for (const auto& dir : dirs) {
        m_db.prepare("INSERT INTO library_include_dirs (ordinal, dir) VALUES (?, ?)")
            .bind(1, ordinal++)
            .bind(2, dir)
            .step();
    }
}

std::vector<std::string> SymbolDatabase::libraryIncludeDirs() const
{
    auto check = m_db.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='library_include_dirs'");
    check.step();
    if (check.columnInt(0) == 0) return {};

    std::vector<std::string> dirs;
    auto stmt = m_db.prepare("SELECT dir FROM library_include_dirs ORDER BY ordinal");
    while (stmt.step()) dirs.push_back(stmt.columnText(0));
    return dirs;
}

void SymbolDatabase::setBuiltByVersion(const std::string& version)
{
    m_db.execute("DELETE FROM library_build_info");
    m_db.prepare("INSERT INTO library_build_info (svlsp_version) VALUES (?)")
        .bind(1, version)
        .step();
}

std::string SymbolDatabase::builtByVersion() const
{
    auto check = m_db.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='library_build_info'");
    check.step();
    if (check.columnInt(0) == 0) return "";

    auto stmt = m_db.prepare("SELECT svlsp_version FROM library_build_info LIMIT 1");
    if (stmt.step()) return stmt.columnText(0);
    return "";
}

void SymbolDatabase::resetAllFiles()
{
    m_db.execute("DELETE FROM files");
    clearIncludeCaches();
}

std::vector<SymbolRow> SymbolDatabase::queryAcrossAttachedDbs(
    const std::string& cond, const std::string& bindValue) const
{
    static constexpr const char* kCols =
        "s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path";

    std::string sql =
        std::string("SELECT ") + kCols + " FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE " + cond;
    for (size_t i = 0; i < m_attachedLibraryPaths.size(); ++i) {
        std::string alias = "lib" + std::to_string(i);
        sql += " UNION ALL SELECT " + std::string(kCols) + " FROM " + alias + ".symbols s "
               "JOIN " + alias + ".files f ON f.id = s.file_id WHERE " + cond;
    }

    auto stmt = m_db.prepare(sql);
    int idx = 1;
    for (size_t i = 0; i <= m_attachedLibraryPaths.size(); ++i)
        stmt.bind(idx++, bindValue);

    std::vector<SymbolRow> rows;
    while (stmt.step()) rows.push_back(rowFromStmt(stmt));
    return rows;
}

int64_t SymbolDatabase::fileIdFor(const std::string& path) const
{
    auto stmt = m_db.prepare("SELECT id FROM files WHERE path = ?");
    stmt.bind(1, path);
    if (stmt.step()) return stmt.columnInt(0);
    return -1;
}

int64_t SymbolDatabase::upsertFile(const std::string& path,
                                   const std::string& contentHash)
{
    // Try to update first; if no row affected, insert.
    auto upd = m_db.prepare(
        "UPDATE files SET content_hash = ?, parsed_at = ? WHERE path = ?");
    upd.bind(1, contentHash)
       .bind(2, static_cast<int64_t>(std::time(nullptr)))
       .bind(3, path);
    upd.step();

    int64_t existing = fileIdFor(path);
    if (existing >= 0) return existing;

    auto ins = m_db.prepare(
        "INSERT INTO files (path, content_hash, parsed_at) VALUES (?, ?, ?)");
    ins.bind(1, path)
       .bind(2, contentHash)
       .bind(3, static_cast<int64_t>(std::time(nullptr)));
    ins.step();
    return m_db.lastInsertRowId();
}

std::string SymbolDatabase::getFileHash(const std::string& path) const
{
    auto stmt = m_db.prepare("SELECT content_hash FROM files WHERE path = ?");
    stmt.bind(1, path);
    if (stmt.step()) return stmt.columnText(0);
    return {};
}

std::vector<std::string> SymbolDatabase::replaceIncludeSites(
    int64_t primaryFileId, const std::string& primaryPath,
    const std::vector<IncludeSiteRecord>& sites)
{
    clearIncludeCaches();
    std::vector<std::string> previous;
    {
        auto old = m_db.prepare("SELECT f.path FROM include_sites s "
                                "JOIN files f ON f.id = s.included_file_id "
                                "WHERE s.primary_file_id = ?");
        old.bind(1, primaryFileId);
        while (old.step()) previous.push_back(old.columnText(0));
    }
    // A known file keeps its content hash (upsertFile would reset it).
    auto idOf = [&](const std::string& p) {
        if (p.empty() || p == primaryPath) return primaryFileId;
        const int64_t id = fileIdFor(p);
        return id >= 0 ? id : upsertFile(p, "");
    };
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM include_sites WHERE primary_file_id = ?");
    del.bind(1, primaryFileId);
    del.step();

    auto ins = m_db.prepare("INSERT INTO include_sites "
                            "(primary_file_id, included_file_id, includer_file_id, line, scope) "
                            "VALUES (?,?,?,?,?)");
    for (const auto& s : sites) {
        ins.reset();
        ins.bind(1, primaryFileId)
           .bind(2, idOf(s.file))
           .bind(3, idOf(s.includer))
           .bind(4, s.line)
           .bind(5, s.scope);
        ins.step();
    }
    m_db.execute("COMMIT");
    return previous;
}

std::vector<IncludeSiteRow> SymbolDatabase::includeSitesOf(const std::string& path) const
{
    if (auto it = m_includeSitesCache.find(path); it != m_includeSitesCache.end())
        return it->second;
    std::vector<IncludeSiteRow> rows;
    for (size_t i = 0; i <= m_attachedLibraryPaths.size(); ++i) {
        const std::string schema = i == 0 ? "" : "lib" + std::to_string(i - 1) + ".";
        auto chk = m_db.prepare("SELECT 1 FROM " + schema +
                                "sqlite_master WHERE type='table' AND name='include_sites'");
        if (!chk.step()) continue; // a library DB built before v16
        auto stmt = m_db.prepare(
            "SELECT p.path, i.path, s.line, s.scope FROM " + schema + "include_sites s "
            "JOIN " + schema + "files f ON f.id = s.included_file_id "
            "JOIN " + schema + "files p ON p.id = s.primary_file_id "
            "JOIN " + schema + "files i ON i.id = s.includer_file_id "
            "WHERE f.path = ? ORDER BY p.path, i.path, s.line");
        stmt.bind(1, path);
        while (stmt.step())
            rows.push_back({stmt.columnText(0), stmt.columnText(1),
                            static_cast<int>(stmt.columnInt(2)), stmt.columnText(3), schema});
    }
    m_includeSitesCache.emplace(path, rows);
    return rows;
}

std::vector<IncludeSiteRecord> SymbolDatabase::includeSitesOfUnit(const std::string& primaryPath) const
{
    if (auto it = m_unitSitesCache.find(primaryPath); it != m_unitSitesCache.end())
        return it->second;
    std::vector<IncludeSiteRecord> rows;
    std::set<std::string> seen;
    for (size_t i = 0; i <= m_attachedLibraryPaths.size(); ++i) {
        const std::string schema = i == 0 ? "" : "lib" + std::to_string(i - 1) + ".";
        auto chk = m_db.prepare("SELECT 1 FROM " + schema +
                                "sqlite_master WHERE type='table' AND name='include_sites'");
        if (!chk.step()) continue; // a library DB built before v16
        auto stmt = m_db.prepare(
            "SELECT f.path, i.path, s.line FROM " + schema + "include_sites s "
            "JOIN " + schema + "files f ON f.id = s.included_file_id "
            "JOIN " + schema + "files p ON p.id = s.primary_file_id "
            "JOIN " + schema + "files i ON i.id = s.includer_file_id "
            "WHERE p.path = ? ORDER BY s.rowid");
        stmt.bind(1, primaryPath);
        while (stmt.step()) {
            std::string file = stmt.columnText(0);
            if (!seen.insert(file).second) continue;
            rows.push_back({std::move(file), stmt.columnText(1),
                            static_cast<int>(stmt.columnInt(2)), ""});
        }
    }
    m_unitSitesCache.emplace(primaryPath, rows);
    return rows;
}

std::optional<IncludeSiteRow> SymbolDatabase::includeContextOf(const std::string& path) const
{
    auto sites = includeSitesOf(path);
    if (sites.empty()) return std::nullopt;
    return std::move(sites.front());
}

std::vector<MacroRow> SymbolDatabase::macrosOfUnit(const IncludeSiteRow& site,
                                                   const std::string& excludePath) const
{
    const std::string& sch = site.schema;
    auto stmt = m_db.prepare(
        "SELECT m.name, m.line, m.col, m.is_function_like, m.params, f.path, m.body, m.doc "
        "FROM " + sch + "macros m JOIN " + sch + "files f ON f.id = m.file_id "
        "WHERE f.path != ? AND (f.path = ? OR f.id IN ("
        "  SELECT fi.included_file_id FROM " + sch + "file_includes fi "
        "  JOIN " + sch + "files p ON p.id = fi.includer_file_id WHERE p.path = ?)) "
        "ORDER BY f.path, m.line");
    stmt.bind(1, excludePath).bind(2, site.primaryPath).bind(3, site.primaryPath);
    std::vector<MacroRow> rows;
    while (stmt.step()) rows.push_back(macroRowFromStmt(stmt));
    return rows;
}

std::vector<ImportRow> SymbolDatabase::importsVisibleFrom(const std::string& path) const
{
    std::vector<ImportRow> rows = importsForFile(path);
    auto site = includeContextOf(path);
    std::vector<std::string> visited = {path};
    while (site && std::find(visited.begin(), visited.end(), site->includerPath) == visited.end()) {
        const std::string& includer = site->includerPath;
        visited.push_back(includer);
        auto stmt = m_db.prepare(
            "SELECT im.pkg_name, im.item, im.is_export, im.scope FROM " + site->schema +
            "imports im JOIN " + site->schema + "files f ON f.id = im.file_id WHERE f.path = ?");
        stmt.bind(1, includer);
        while (stmt.step())
            rows.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnInt(2) != 0,
                            stmt.columnText(3)});
        if (includer == site->primaryPath) break;
        // The includer's own `include, in the same compile.
        std::optional<IncludeSiteRow> next;
        for (auto& s : includeSitesOf(includer))
            if (s.primaryPath == site->primaryPath && s.schema == site->schema) {
                next = std::move(s);
                break;
            }
        site = std::move(next);
    }
    return rows;
}

bool SymbolDatabase::isPackage(const std::string& scope) const
{
    if (scope.empty() || scope.find("::") != std::string::npos) return false;
    for (const auto& r : findSymbolsByName(scope))
        if (r.kind == "Package" && r.scope.empty()) return true;
    return false;
}

std::vector<std::string> SymbolDatabase::allFilePaths() const
{
    auto stmt = m_db.prepare("SELECT path FROM files");
    std::vector<std::string> paths;
    while (stmt.step())
        paths.push_back(stmt.columnText(0));
    return paths;
}

void SymbolDatabase::replaceSymbols(int64_t fileId,
                                    const std::vector<ParseRecord>& records)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM symbols WHERE file_id = ?");
    del.bind(1, fileId);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO symbols "
        "(file_id,kind,name,line,col,parent,detail,end_line,scope,access,end_col) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto& r : records) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, kindStr(r.kind))
           .bind(3, r.name)
           .bind(4, r.line)
           .bind(5, r.column)
           .bind(6, r.parent)
           .bind(7, r.detail)
           .bind(8, r.endLine)
           .bind(9, r.scope)
           .bind(10, r.access)
           .bind(11, r.endCol);
        ins.step();
    }

    auto delDocs = m_db.prepare("DELETE FROM symbol_docs WHERE file_id = ?");
    delDocs.bind(1, fileId);
    delDocs.step();
    auto insDoc = m_db.prepare(
        "INSERT OR REPLACE INTO symbol_docs (file_id, line, col, doc) VALUES (?,?,?,?)");
    for (const auto& r : records) {
        if (r.doc.empty()) continue;
        insDoc.reset();
        insDoc.bind(1, fileId).bind(2, r.line).bind(3, r.column).bind(4, r.doc);
        insDoc.step();
    }
    m_db.execute("COMMIT");
}

std::string SymbolDatabase::docFor(const SymbolRow& sym) const
{
    for (size_t i = 0; i <= m_attachedLibraryPaths.size(); ++i) {
        const std::string schema = i == 0 ? "main" : "lib" + std::to_string(i - 1);
        if (i > 0) {
            auto chk = m_db.prepare("SELECT 1 FROM " + schema +
                                    ".sqlite_master WHERE type='table' AND name='symbol_docs'");
            if (!chk.step()) continue; // built before schema v11
        }
        auto stmt = m_db.prepare("SELECT d.doc FROM " + schema + ".symbol_docs d JOIN " + schema +
                                 ".files f ON f.id = d.file_id "
                                 "WHERE f.path = ? AND d.line = ? AND d.col = ?");
        stmt.bind(1, sym.filePath).bind(2, sym.line).bind(3, sym.col);
        if (stmt.step()) return stmt.columnText(0);
    }
    return "";
}

std::unordered_map<std::string, std::string> SymbolDatabase::memberAccess(
    const std::string& scope) const
{
    std::unordered_map<std::string, std::string> access;
    for (size_t i = 0; i <= m_attachedLibraryPaths.size(); ++i) {
        const std::string schema = i == 0 ? "main" : "lib" + std::to_string(i - 1);
        if (i > 0) {
            auto chk = m_db.prepare("SELECT 1 FROM pragma_table_info('symbols', '" + schema +
                                    "') WHERE name = 'access'");
            if (!chk.step()) continue; // built before schema v13
        }
        auto stmt = m_db.prepare("SELECT name, access FROM " + schema +
                                 ".symbols WHERE scope = ? AND access <> ''");
        stmt.bind(1, scope);
        while (stmt.step()) access.emplace(stmt.columnText(0), stmt.columnText(1));
    }
    return access;
}

void SymbolDatabase::replaceDiagnostics(int64_t fileId,
                                        const std::vector<ParseError>& errors,
                                        const std::string& source)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM diagnostics WHERE file_id = ? AND source = ?");
    del.bind(1, fileId).bind(2, source);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO diagnostics (file_id,line,col,message,source,severity) "
        "VALUES (?,?,?,?,?,?)");
    for (const auto& e : errors) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, e.line)
           .bind(3, e.column)
           .bind(4, e.message)
           .bind(5, source)
           .bind(6, e.severity);
        ins.step();
    }
    m_db.execute("COMMIT");
}

void SymbolDatabase::replaceImports(int64_t fileId,
                                    const std::vector<ImportRecord>& imports)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM imports WHERE file_id = ?");
    del.bind(1, fileId);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO imports (file_id, pkg_name, item, is_export, scope) VALUES (?,?,?,?,?)");
    for (const auto& imp : imports) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, imp.pkgName)
           .bind(3, imp.item)
           .bind(4, imp.isExport ? 1 : 0)
           .bind(5, imp.scope);
        ins.step();
    }
    m_db.execute("COMMIT");
}

namespace {
constexpr char kMacroParamSep = '\x1f';

// A LIKE pattern matching names that start with `prefix` literally, for use
// with `ESCAPE '\'` -- '_' is common in SystemVerilog names.
std::string likePrefixPattern(const std::string& prefix)
{
    std::string out;
    for (char c : prefix) {
        if (c == '\\' || c == '%' || c == '_') out += '\\';
        out += c;
    }
    return out + "%";
}
}

void SymbolDatabase::replaceMacros(int64_t fileId, const std::vector<MacroRecord>& macros)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM macros WHERE file_id = ?");
    del.bind(1, fileId);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO macros (file_id, name, line, col, is_function_like, params, body, doc) "
        "VALUES (?,?,?,?,?,?,?,?)");
    for (const auto& m : macros) {
        std::string params;
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (i > 0) params += kMacroParamSep;
            params += m.params[i];
            if (i < m.defaults.size() && m.defaults[i]) params += "=" + *m.defaults[i];
        }
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, m.name)
           .bind(3, m.line)
           .bind(4, m.column)
           .bind(5, m.isFunctionLike ? 1 : 0)
           .bind(6, params)
           .bind(7, m.body)
           .bind(8, m.doc);
        ins.step();
    }
    m_db.execute("COMMIT");
}

std::vector<MacroRow> SymbolDatabase::findMacros(const std::string& name) const
{
    auto rows = queryMacros("m.name = ?", name);
    std::stable_sort(rows.begin(), rows.end(), [](const MacroRow& a, const MacroRow& b) {
        return a.filePath != b.filePath ? a.filePath < b.filePath : a.line < b.line;
    });
    return rows;
}

std::vector<MacroRow> SymbolDatabase::findMacrosByNamePrefix(const std::string& prefix) const
{
    auto rows = queryMacros("m.name LIKE ? ESCAPE '\\'", likePrefixPattern(prefix));
    std::stable_sort(rows.begin(), rows.end(), [](const MacroRow& a, const MacroRow& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.filePath != b.filePath ? a.filePath < b.filePath : a.line < b.line;
    });
    return rows;
}

std::vector<MacroRow> SymbolDatabase::queryMacros(const std::string& cond,
                                                  const std::string& bindValue) const
{
    auto select = [](const std::string& bodyCol, const std::string& docCol) {
        return "SELECT m.name, m.line, m.col, m.is_function_like, m.params, f.path, " + bodyCol +
               ", " + docCol + " FROM ";
    };
    std::string sql = select("m.body", "m.doc") +
        "macros m JOIN files f ON f.id = m.file_id WHERE " + cond;
    int binds = 1;
    for (size_t i = 0; i < m_attachedLibraryPaths.size(); ++i) {
        const std::string alias = "lib" + std::to_string(i);
        auto chk = m_db.prepare("SELECT sql FROM " + alias +
                                ".sqlite_master WHERE type='table' AND name='macros'");
        if (!chk.step()) continue; // schema < 9: no macros table
        const std::string ddl = chk.columnText(0);
        const bool hasBody = ddl.find("body") != std::string::npos;
        const bool hasDoc  = ddl.find("doc") != std::string::npos;
        sql += " UNION ALL " + select(hasBody ? "m.body" : "''", hasDoc ? "m.doc" : "''") + alias + ".macros m JOIN " +
               alias + ".files f ON f.id = m.file_id WHERE " + cond;
        ++binds;
    }

    auto stmt = m_db.prepare(sql);
    for (int i = 1; i <= binds; ++i) stmt.bind(i, bindValue);

    std::vector<MacroRow> rows;
    while (stmt.step()) rows.push_back(macroRowFromStmt(stmt));
    return rows;
}

// Columns: name, line, col, is_function_like, params, path, body, doc.
MacroRow SymbolDatabase::macroRowFromStmt(Database::Statement& stmt)
{
    MacroRow row{stmt.columnText(0), static_cast<int>(stmt.columnInt(1)),
                 static_cast<int>(stmt.columnInt(2)), stmt.columnInt(3) != 0,
                 {}, {}, stmt.columnText(5), stmt.columnText(6), stmt.columnText(7)};
    const std::string params = stmt.columnText(4);
    if (row.isFunctionLike && !params.empty()) {
        for (size_t start = 0;;) {
            size_t sep = params.find(kMacroParamSep, start);
            std::string p = params.substr(start, sep == std::string::npos ? std::string::npos
                                                                          : sep - start);
            size_t eq = p.find('=');
            row.params.push_back(p.substr(0, eq));
            row.defaults.push_back(eq == std::string::npos
                                       ? std::nullopt
                                       : std::optional<std::string>(p.substr(eq + 1)));
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    }
    return row;
}

std::vector<SymbolRow> SymbolDatabase::portsOf(const std::string& scope) const
{
    std::vector<SymbolRow> ports;
    for (auto& row : findSymbolsInScope(scope))
        if (row.kind == "Port") ports.push_back(row);
    std::sort(ports.begin(), ports.end(), [](const SymbolRow& a, const SymbolRow& b) {
        if (a.filePath != b.filePath) return a.filePath < b.filePath;
        return a.line != b.line ? a.line < b.line : a.col < b.col;
    });
    std::vector<SymbolRow> first;
    for (auto& p : ports) {
        const bool repeated = std::any_of(first.begin(), first.end(),
                                          [&](const SymbolRow& q) { return q.name == p.name; });
        if (repeated) break;
        first.push_back(std::move(p));
    }
    return first;
}

std::vector<ImportRow> SymbolDatabase::importsForFile(const std::string& path) const
{
    const int64_t fid = fileIdFor(path);
    return fid < 0 ? std::vector<ImportRow>{} : importsForFileId(fid);
}

std::vector<ImportRow> SymbolDatabase::importsForFileId(int64_t fileId) const
{
    auto stmt = m_db.prepare(
        "SELECT pkg_name, item, is_export, scope FROM imports WHERE file_id = ?");
    stmt.bind(1, fileId);
    std::vector<ImportRow> rows;
    while (stmt.step())
        rows.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnInt(2) != 0,
                        stmt.columnText(3)});
    return rows;
}

void SymbolDatabase::replaceInstantiations(int64_t fileId,
                                           const std::vector<InstantiationRecord>& insts)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM instantiations WHERE file_id = ?");
    del.bind(1, fileId);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO instantiations (file_id, type_name, inst_name, line) VALUES (?,?,?,?)");
    for (const auto& inst : insts) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, inst.typeName)
           .bind(3, inst.instName)
           .bind(4, inst.line);
        ins.step();
    }
    m_db.execute("COMMIT");
}

std::vector<std::string> SymbolDatabase::unresolvedInstantiatedTypeNames() const
{
    auto stmt = m_db.prepare(
        "SELECT DISTINCT i.type_name FROM instantiations i "
        "WHERE NOT EXISTS ("
        "  SELECT 1 FROM symbols s "
        "  WHERE s.name = i.type_name AND s.kind IN ('Module','Interface','Program')"
        ")");
    std::vector<std::string> names;
    while (stmt.step()) names.push_back(stmt.columnText(0));
    return names;
}

std::vector<InstantiationRow> SymbolDatabase::instantiationsOfType(
    const std::string& typeName) const
{
    auto stmt = m_db.prepare(
        "SELECT i.file_id, f.path, i.line FROM instantiations i "
        "JOIN files f ON f.id = i.file_id WHERE i.type_name = ?");
    stmt.bind(1, typeName);
    std::vector<InstantiationRow> rows;
    while (stmt.step())
        rows.push_back({stmt.columnInt(0), stmt.columnText(1),
                        static_cast<int>(stmt.columnInt(2))});
    return rows;
}

void SymbolDatabase::appendDiagnostics(int64_t fileId, const std::vector<ParseError>& extra,
                                       const std::string& source, const std::string& subject)
{
    m_db.execute("BEGIN");
    auto ins = m_db.prepare("INSERT INTO diagnostics "
                            "(file_id,line,col,message,source,subject,severity) "
                            "VALUES (?,?,?,?,?,?,?)");
    for (const auto& e : extra) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, e.line)
           .bind(3, e.column)
           .bind(4, e.message)
           .bind(5, source)
           .bind(6, subject)
           .bind(7, e.severity);
        ins.step();
    }
    m_db.execute("COMMIT");
}

void SymbolDatabase::clearDiagnostics(const std::string& source)
{
    auto del = m_db.prepare("DELETE FROM diagnostics WHERE source = ?");
    del.bind(1, source);
    del.step();
}

std::vector<ParseError> SymbolDatabase::refreshLibraryDiagnostics(int64_t fileId)
{
    std::vector<std::pair<std::string, std::string>> tagged; // (module name, message)
    {
        auto q = m_db.prepare(
            "SELECT subject, MIN(message) FROM diagnostics "
            "WHERE file_id = ? AND source = 'library' GROUP BY subject");
        q.bind(1, fileId);
        while (q.step()) tagged.emplace_back(q.columnText(0), q.columnText(1));
    }
    if (tagged.empty()) return {};

    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM diagnostics WHERE file_id = ? AND source = 'library'");
    del.bind(1, fileId);
    del.step();

    std::vector<ParseError> kept;
    auto declared = m_db.prepare(
        "SELECT 1 FROM symbols WHERE name = ? AND kind IN ('Module','Interface','Program') LIMIT 1");
    auto lines = m_db.prepare(
        "SELECT line FROM instantiations WHERE file_id = ? AND type_name = ? ORDER BY line");
    auto ins = m_db.prepare("INSERT INTO diagnostics (file_id,line,col,message,source,subject) "
                            "VALUES (?,?,?,?,'library',?)");
    for (const auto& [typeName, message] : tagged) {
        declared.reset();
        declared.bind(1, typeName);
        if (declared.step()) continue;
        lines.reset();
        lines.bind(1, fileId).bind(2, typeName);
        while (lines.step()) {
            const int line = static_cast<int>(lines.columnInt(0));
            ins.reset();
            ins.bind(1, fileId).bind(2, line).bind(3, 0).bind(4, message).bind(5, typeName);
            ins.step();
            kept.push_back({line, 0, message});
        }
    }
    m_db.execute("COMMIT");
    return kept;
}

void SymbolDatabase::replaceFileIncludes(int64_t fileId,
                                         const std::vector<std::string>& includedPaths)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM file_includes WHERE includer_file_id = ?");
    del.bind(1, fileId);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO file_includes (includer_file_id, included_file_id) VALUES (?,?)");
    for (const auto& path : includedPaths) {
        int64_t includedId = fileIdFor(path);
        if (includedId < 0) continue; // shouldn't happen -- caller already upserted it
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, includedId);
        ins.step();
    }
    m_db.execute("COMMIT");
}

std::vector<std::string> SymbolDatabase::includersOf(const std::string& path) const
{
    int64_t fid = fileIdFor(path);
    if (fid < 0) return {};

    auto stmt = m_db.prepare(
        "SELECT f.path FROM file_includes fi "
        "JOIN files f ON f.id = fi.includer_file_id "
        "WHERE fi.included_file_id = ?");
    stmt.bind(1, fid);

    std::vector<std::string> result;
    while (stmt.step())
        result.push_back(stmt.columnText(0));
    return result;
}

int64_t SymbolDatabase::fileIdForPackage(const std::string& pkgName) const
{
    auto stmt = m_db.prepare(
        "SELECT file_id FROM symbols WHERE kind = 'Package' AND name = ? LIMIT 1");
    stmt.bind(1, pkgName);
    if (stmt.step()) return stmt.columnInt(0);
    return -1;
}

void SymbolDatabase::collectExportedImports(
    const std::string& pkgName,
    std::vector<std::string>& outWildcardPkgs,
    std::vector<std::pair<std::string, std::string>>& outSpecific,
    std::vector<std::string>& visited) const
{
    if (std::find(visited.begin(), visited.end(), pkgName) != visited.end())
        return;
    visited.push_back(pkgName);

    int64_t fid = fileIdForPackage(pkgName);
    if (fid < 0) return;

    // Only this package's own exports: its file may declare other packages.
    // (A pre-v14 row has no scope and is taken as before.)
    for (auto& imp : importsForFileId(fid)) {
        if (!imp.isExport || (imp.scope != pkgName && !imp.scope.empty())) continue;
        if (imp.item == "*") {
            outWildcardPkgs.push_back(imp.pkgName);
            collectExportedImports(imp.pkgName, outWildcardPkgs, outSpecific, visited);
        } else {
            outSpecific.emplace_back(imp.pkgName, imp.item);
        }
    }
}

std::vector<SymbolRow> SymbolDatabase::symbolsForFile(
    const std::string& path) const
{
    int64_t fid = fileIdFor(path);
    if (fid < 0) return {};

    auto stmt = m_db.prepare(
        "SELECT id,kind,name,line,col,parent,detail,end_line,scope FROM symbols "
        "WHERE file_id = ? ORDER BY line");
    stmt.bind(1, fid);

    std::vector<SymbolRow> rows;
    while (stmt.step()) {
        rows.push_back({stmt.columnInt(0),
                        stmt.columnText(1),
                        stmt.columnText(2),
                        static_cast<int>(stmt.columnInt(3)),
                        static_cast<int>(stmt.columnInt(4)),
                        stmt.columnText(5),
                        stmt.columnText(6),
                        path,
                        static_cast<int>(stmt.columnInt(7)),
                        stmt.columnText(8)});
    }
    return rows;
}

std::vector<SymbolRow> SymbolDatabase::findSymbolsByName(
    const std::string& name) const
{
    auto rows = queryAcrossAttachedDbs("s.name = ?", name);
    std::stable_sort(rows.begin(), rows.end(), [](const SymbolRow& a, const SymbolRow& b) {
        if (a.filePath != b.filePath) return a.filePath < b.filePath;
        return a.line < b.line;
    });
    return rows;
}

std::vector<DiagnosticRow> SymbolDatabase::diagnosticsForFile(
    const std::string& path) const
{
    int64_t fid = fileIdFor(path);
    if (fid < 0) return {};

    auto stmt = m_db.prepare(
        "SELECT line,col,message,severity FROM diagnostics WHERE file_id = ? ORDER BY line");
    stmt.bind(1, fid);

    std::vector<DiagnosticRow> rows;
    while (stmt.step())
        rows.push_back({static_cast<int>(stmt.columnInt(0)),
                        static_cast<int>(stmt.columnInt(1)),
                        stmt.columnText(2),
                        path,
                        static_cast<int>(stmt.columnInt(3))});
    return rows;
}

// ---------------------------------------------------------------------------
// Context-aware query helpers
// ---------------------------------------------------------------------------

std::vector<SymbolRow> SymbolDatabase::findSymbolsInScope(
    const std::string& scope) const
{
    auto rows = queryAcrossAttachedDbs("s.scope = ?", scope);
    std::stable_sort(rows.begin(), rows.end(), [](const SymbolRow& a, const SymbolRow& b) {
        return a.name < b.name;
    });
    return rows;
}

std::vector<SymbolRow> SymbolDatabase::findSymbolsByNamePrefix(
    const std::string& prefix) const
{
    auto rows = queryAcrossAttachedDbs("s.name LIKE ? ESCAPE '\\'", likePrefixPattern(prefix));
    std::stable_sort(rows.begin(), rows.end(), [](const SymbolRow& a, const SymbolRow& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.filePath < b.filePath;
    });
    return rows;
}

namespace {

// WHERE fragment: scope row `s` contains the position bound by
// bindContains -- from its (line, col) through (end_line, end_col), the
// whole end line when end_col is 0 (unknown: a row written before v15).
constexpr const char* kContains =
    "  AND (s.line < ? OR (s.line = ? AND s.col <= ?)) "
    "  AND (s.end_line > ? OR (s.end_line = ? AND (s.end_col = 0 OR s.end_col >= ?))) ";

void bindContains(Database::Statement& stmt, int first, SrcPos pos)
{
    stmt.bind(first, pos.line).bind(first + 1, pos.line).bind(first + 2, pos.col)
        .bind(first + 3, pos.line).bind(first + 4, pos.line).bind(first + 5, pos.col);
}

} // namespace

std::string SymbolDatabase::scopeAtPosition(
    const std::string& path, SrcPos pos, bool withBlocks) const
{
    // Find the innermost scope-defining symbol (Module/Interface/Program/
    // Package/Class/Function/Task, and Block unless !withBlocks) that
    // contains `pos`.  Deepest nesting wins (longest scope chain).
    auto stmt = m_db.prepare(
        std::string("SELECT CASE WHEN s.scope = '' THEN s.name ELSE s.scope || '::' || s.name END "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? "
        "  AND s.kind IN ('Module','Interface','Program','Package','Class','Function','Task'") +
        (withBlocks ? ",'Block'" : "") + ") " + kContains +
        "ORDER BY length(s.scope) DESC, s.line DESC, s.col DESC "
        "LIMIT 1");
    stmt.bind(1, path);
    bindContains(stmt, 2, pos);
    if (stmt.step()) return stmt.columnText(0);
    const auto site = includeContextOf(path);
    return site ? site->scope : std::string{};
}

std::string SymbolDatabase::scopeKindAtPosition(
    const std::string& path, SrcPos pos) const
{
    // Same shape as scopeAtPosition (and the same kind list) but selects the
    // kind of the innermost scope-defining symbol instead of its full name
    // path.
    auto stmt = m_db.prepare(
        std::string("SELECT s.kind "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? "
        "  AND s.kind IN ('Module','Interface','Program','Package','Class','Function','Task') ") +
        kContains +
        "ORDER BY length(s.scope) DESC, s.line DESC, s.col DESC "
        "LIMIT 1");
    stmt.bind(1, path);
    bindContains(stmt, 2, pos);
    if (stmt.step()) return stmt.columnText(0);

    // Outside the file's own scopes: the kind of its include scope's row.
    const auto site = includeContextOf(path);
    const std::string inc = site ? site->scope : std::string{};
    if (inc.empty()) return {};
    const auto sep = inc.rfind("::");
    const std::string name  = sep == std::string::npos ? inc : inc.substr(sep + 2);
    const std::string scope = sep == std::string::npos ? "" : inc.substr(0, sep);
    static const std::set<std::string> kScopeKinds = {
        "Module", "Interface", "Program", "Package", "Class", "Function", "Task"};
    for (const auto& r : findSymbolsByName(name))
        if (r.scope == scope && kScopeKinds.count(r.kind)) return r.kind;
    return {};
}

std::vector<std::string> SymbolDatabase::enclosingClassScopesAt(
    const std::string& path, SrcPos pos) const
{
    // scopeAtPosition already lands inside an out-of-class body's class
    // ("p::C::m"); keep each scope on its chain that is a Class row.
    std::vector<std::string> classes;
    const std::string innermost = scopeAtPosition(path, pos);
    if (innermost.empty()) return classes;
    for (const auto& cur : scopeChain(innermost)) {
        if (cur.empty()) break;
        const auto sep = cur.rfind("::");
        const std::string name  = sep == std::string::npos ? cur : cur.substr(sep + 2);
        const std::string scope = sep == std::string::npos ? "" : cur.substr(0, sep);
        for (const auto& r : findSymbolsByName(name))
            if (r.kind == "Class" && r.scope == scope) {
                classes.push_back(cur);
                break;
            }
    }
    return classes;
}

std::string SymbolDatabase::enclosingClassNameAt(
    const std::string& path, SrcPos pos) const
{
    // Same shape as scopeAtPosition/scopeKindAtPosition, but filtered to
    // Class specifically -- finds the nearest enclosing class even when the
    // innermost scope at `line` is one of its Function/Task members. Falls
    // back to the scope chain for an out-of-class method body (below).
    auto stmt = m_db.prepare(
        std::string("SELECT s.name "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? "
        "  AND s.kind = 'Class' ") + kContains +
        "ORDER BY length(s.scope) DESC, s.line DESC, s.col DESC "
        "LIMIT 1");
    stmt.bind(1, path);
    bindContains(stmt, 2, pos);
    if (stmt.step()) return stmt.columnText(0);

    // An out-of-class method body (plan.md §6.30 step D) lies outside its
    // class's line range but is scoped under it: the innermost scope is
    // "<class scope>::m", so find the nearest class on the scope chain.
    std::string cur = scopeAtPosition(path, pos);
    for (auto sep = cur.rfind("::"); sep != std::string::npos; sep = cur.rfind("::")) {
        cur = cur.substr(0, sep);
        const auto nameSep = cur.rfind("::");
        const std::string name  = nameSep == std::string::npos ? cur : cur.substr(nameSep + 2);
        const std::string scope = nameSep == std::string::npos ? "" : cur.substr(0, nameSep);
        for (const auto& r : findSymbolsByName(name))
            if (r.kind == "Class" && r.scope == scope) return name;
    }
    return {};
}

// A position's place in its compilation unit's text order: the `include
// line of each file from the primary down, then (line, col) in the file
// itself. An `include line sorts after everything else written on that
// line of its includer, so the included text comes after the include line
// and before the next one. Compared lexicographically.
using UnitKey = std::vector<std::pair<int, int>>;

struct UnitOrder {
    // Each file of the unit's key prefix (the primary's is empty).
    std::unordered_map<std::string, UnitKey> prefixes;

    UnitOrder(const std::string& primary, const std::vector<IncludeSiteRecord>& sites)
    {
        std::unordered_map<std::string, std::pair<std::string, int>> site; // file -> (includer, line)
        for (const auto& s : sites) site.emplace(s.file, std::make_pair(s.includer, s.line));
        prefixes.emplace(primary, UnitKey{});
        for (const auto& s : sites) {
            UnitKey prefix;
            std::string cur = s.file;
            bool ok = true;
            for (int depth = 0; cur != primary; ++depth) {
                auto it = site.find(cur);
                if (it == site.end() || depth > 64) { ok = false; break; }
                prefix.emplace_back(it->second.second, std::numeric_limits<int>::max());
                cur = it->second.first;
            }
            if (!ok) continue;
            std::reverse(prefix.begin(), prefix.end());
            prefixes.emplace(s.file, std::move(prefix));
        }
    }

    // `file`'s key prefix, null for a file outside the unit.
    const UnitKey* prefixOf(const std::string& file) const
    {
        auto it = prefixes.find(file);
        return it == prefixes.end() ? nullptr : &it->second;
    }
};

namespace {

// (a, at) <= (b, bt) as unit keys: prefix + the position itself.
bool keyLessEq(const UnitKey& a, std::pair<int, int> at, const UnitKey& b, std::pair<int, int> bt)
{
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return a[i] < b[i];
    const auto& x = a.size() > n ? a[n] : at;
    const auto& y = b.size() > n ? b[n] : bt;
    if (a.size() == b.size()) return at <= bt;
    return x < y || (x == y && a.size() < b.size());
}

} // namespace

const UnitOrder& SymbolDatabase::unitOrderFor(const std::string& path) const
{
    // The unit's primary file: follow include contexts up from `path` (an
    // included file compiled on its own records its own includes under
    // itself, so its includees' first context may be it).
    auto pit = m_unitPrimaryCache.find(path);
    if (pit == m_unitPrimaryCache.end()) {
        std::string primary = path;
        std::set<std::string> visited{path};
        while (auto site = includeContextOf(primary)) {
            if (!visited.insert(site->primaryPath).second) break;
            primary = site->primaryPath;
        }
        pit = m_unitPrimaryCache.emplace(path, std::move(primary)).first;
    }
    const std::string& primary = pit->second;
    auto oit = m_unitOrderCache.find(primary);
    if (oit == m_unitOrderCache.end())
        oit = m_unitOrderCache
                  .emplace(primary, std::make_shared<const UnitOrder>(primary,
                                                                      includeSitesOfUnit(primary)))
                  .first;
    return *oit->second;
}

void SymbolDatabase::keepDeclaredBefore(std::vector<SymbolRow>& rows, const std::string& path,
                                        SrcPos pos, const std::vector<std::string>& scopes) const
{
    const UnitOrder& order = unitOrderFor(path);

    // Rows are only marked here and compacted once at the end, so views
    // into them stay valid throughout. Forward rows are few: key sets hold
    // only their (scope, name)s, and a row's name is checked against
    // `forwardNames` before any pair lookup.
    using Key = std::pair<std::string_view, std::string_view>; // (scope, name)
    std::unordered_set<std::string_view> forwardNames;
    for (const auto& r : rows)
        if (isForwardTypedef(r)) forwardNames.insert(r.name);
    const bool anyForward = !forwardNames.empty();
    auto keyIn = [&](const std::set<Key>& set, const SymbolRow& r) {
        return forwardNames.count(r.name) && set.count({r.scope, r.name});
    };
    std::vector<char> drop(rows.size(), 0);

    if (const UnitKey* cursor = order.prefixOf(path)) {
        const std::pair<int, int> cursorAt{pos.line, pos.col};
        // The classes on the chain: each one's own row is among `rows` (a
        // chain scope's parent is on the chain too, and the class is in the
        // cursor's file, its package or the top level).
        std::vector<std::string> classes;
        for (const auto& r : rows)
            if (r.kind == "Class") {
                const std::string q = r.scope.empty() ? r.name : r.scope + "::" + r.name;
                if (std::find(scopes.begin(), scopes.end(), q) != scopes.end()) classes.push_back(q);
            }
        auto in = [](const std::vector<std::string>& v, const std::string& s) {
            return std::find(v.begin(), v.end(), s) != v.end();
        };
        auto subject = [&](const SymbolRow& r) {
            const std::string& k = r.kind;
            return (k == "Signal" || k == "Parameter" || k == "Typedef" || k == "Class" ||
                    k == "EnumLiteral" || k == "Genvar") &&
                   in(scopes, r.scope) && !in(classes, r.scope);
        };
        // Rows come grouped by file: remember the last file's prefix.
        std::string_view lastFile;
        const UnitKey* lastPrefix = nullptr;
        bool haveLast = false;
        auto before = [&](const SymbolRow& r) {
            if (!haveLast || lastFile != r.filePath) {
                lastFile = r.filePath;
                lastPrefix = order.prefixOf(r.filePath);
                haveLast = true;
            }
            const UnitKey* prefix = lastPrefix;
            return !prefix || // outside the unit: exempt
                   keyLessEq(*prefix, {r.line, r.col}, *cursor, cursorAt);
        };
        std::set<Key> forwardDeclared;
        if (anyForward)
            for (const auto& r : rows)
                if (isForwardTypedef(r) && (!subject(r) || before(r)))
                    forwardDeclared.insert({r.scope, r.name});
        for (size_t i = 0; i < rows.size(); ++i)
            if (subject(rows[i]) && !before(rows[i]) && !keyIn(forwardDeclared, rows[i]))
                drop[i] = 1;
    }

    if (anyForward) {
        std::set<Key> real;
        for (size_t i = 0; i < rows.size(); ++i)
            if (!drop[i] && !isForwardTypedef(rows[i]) && forwardNames.count(rows[i].name))
                real.insert({rows[i].scope, rows[i].name});
        for (size_t i = 0; i < rows.size(); ++i)
            if (!drop[i] && isForwardTypedef(rows[i]) && real.count({rows[i].scope, rows[i].name}))
                drop[i] = 1;
    }

    size_t kept = 0;
    for (size_t i = 0; i < rows.size(); ++i)
        if (!drop[i]) {
            if (kept != i) rows[kept] = std::move(rows[i]);
            ++kept;
        }
    rows.resize(kept);
}

std::vector<SymbolRow> SymbolDatabase::findSymbolsVisibleAt(
    const std::string& path, SrcPos pos) const
{
    // The scope chain from innermost outward, ending with "".
    const std::vector<std::string> scopes = scopeChain(scopeAtPosition(path, pos));

    // Load the imports that apply here: wildcard (`import pkg::*`) and
    // specific (`import pkg::Name`), each only if the scope it is written
    // in is on the cursor's scope chain -- an import in one module doesn't
    // reach the next one (LRM 26.3). Wildcards extend the cross-file scope
    // search; specific imports are fetched with an extra name-filtered
    // UNION ALL.
    int64_t fid = fileIdFor(path);
    std::vector<std::string> wildcardPkgs;
    std::vector<std::pair<std::string, std::string>> specificImports; // {pkg, name}
    if (fid >= 0) {
        for (auto& imp : importsVisibleFrom(path)) {
            if (std::find(scopes.begin(), scopes.end(), imp.scope) == scopes.end()) continue;
            if (imp.item == "*")
                wildcardPkgs.push_back(imp.pkgName);
            else
                specificImports.emplace_back(imp.pkgName, imp.item);
        }
    }

    // Follow `export pkg::*` / `export pkg::item` declarations inside each
    // wildcard-imported package, so re-exported symbols become visible too.
    // Plain (non-exported) imports of an imported package are intentionally
    // NOT followed — only the immediately imported scope is visible unless
    // that scope explicitly re-exports it.
    {
        std::vector<std::string> visited;
        // Copy: collectExportedImports may append to wildcardPkgs while we
        // iterate the original set of directly wildcard-imported packages.
        auto directWildcards = wildcardPkgs;
        for (auto& pkg : directWildcards)
            collectExportedImports(pkg, wildcardPkgs, specificImports, visited);
    }

    // The packages on the chain: their members are visible from every file,
    // not just this one -- a package is often spread over `include'd files
    // (uvm_pkg over ~150), each of whose rows is attributed to its own file.
    std::vector<std::string> chainPkgs;
    for (const auto& sc : scopes)
        if (isPackage(sc)) chainPkgs.push_back(sc);

    // Part 1 placeholders: one ? per local scope.
    std::string localPh;
    for (size_t i = 0; i < scopes.size(); ++i) { if (i) localPh += ','; localPh += '?'; }
    std::string chainPkgPh;
    for (size_t i = 0; i < chainPkgs.size(); ++i) { if (i) chainPkgPh += ','; chainPkgPh += '?'; }

    // Part 2 placeholders: one ? per wildcard-imported package.
    std::string wildPh;
    for (size_t i = 0; i < wildcardPkgs.size(); ++i) { if (i) wildPh += ','; wildPh += '?'; }

    // SQLite does not allow expressions in ORDER BY after UNION ALL; sort in C++.
    // Part 2 (top-level + wildcard-package scopes) and Part 3 (specific
    // imports) also search every attached library schema (plan.md §6.19) --
    // one arm per schema, "" (main, unqualified table names) first, then
    // "lib0.", "lib1.", ... Part 1 (the local scope chain) never does: the
    // cursor's own file is always in this connection's main schema, never a
    // read-only attached one.
    std::vector<std::string> schemas = {""};
    for (size_t i = 0; i < m_attachedLibraryPaths.size(); ++i)
        schemas.push_back("lib" + std::to_string(i) + ".");

    std::string sql =
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? AND s.scope IN (" + localPh + ")";
    // Part 1b: the chain's packages' members from every other file.
    if (!chainPkgs.empty())
        sql += " UNION ALL "
               "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
               "FROM symbols s JOIN files f ON f.id = s.file_id "
               "WHERE f.path != ? AND s.scope IN (" + chainPkgPh + ")";

    // Part 2: top-level symbols from every *other* file (this file's own are
    // already in Part 1's chain, which always ends with ""), plus every
    // wildcard-imported package's members from *any* file -- including this
    // one, since a package can be declared and imported in the same file
    // (plan.md §6.30 step 1). The one exclusion keeps a wildcard package
    // that is also on the cursor's own scope chain (the cursor is inside the
    // imported package itself) from returning its rows twice.
    // In the main schema, Part 1b already has a chain package's rows from
    // the other files too.
    std::string part2Cond = "(f.path != ? AND s.scope = '')";
    if (!wildcardPkgs.empty())
        part2Cond += " OR (s.scope IN (" + wildPh + ") AND NOT (f.path = ? AND s.scope IN (" +
                     localPh + ")))";
    for (const auto& schema : schemas) {
        std::string cond = part2Cond;
        if (schema.empty() && !wildcardPkgs.empty() && !chainPkgs.empty())
            cond = "(" + cond + ") AND s.scope NOT IN (" + chainPkgPh + ")";
        sql += " UNION ALL "
               "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
               "FROM " + schema + "symbols s JOIN " + schema + "files f ON f.id = s.file_id "
               "WHERE " + cond;
    }

    // Part 3: one UNION ALL per specific import, per schema, filtered by scope + name.
    for (const auto& schema : schemas)
        for (size_t i = 0; i < specificImports.size(); ++i)
            sql += " UNION ALL "
                   "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
                   "FROM " + schema + "symbols s JOIN " + schema + "files f ON f.id = s.file_id "
                   "WHERE s.scope = ? AND s.name = ?";

    auto stmt = m_db.prepare(sql);
    int idx = 1;
    stmt.bind(idx++, path);
    for (const auto& sc : scopes) stmt.bind(idx++, sc);
    if (!chainPkgs.empty()) {
        stmt.bind(idx++, path);
        for (const auto& pkg : chainPkgs) stmt.bind(idx++, pkg);
    }
    for (size_t i = 0; i < schemas.size(); ++i) {
        stmt.bind(idx++, path);
        if (wildcardPkgs.empty()) continue;
        for (const auto& pkg : wildcardPkgs) stmt.bind(idx++, pkg);
        stmt.bind(idx++, path);
        for (const auto& sc : scopes) stmt.bind(idx++, sc);
        if (i == 0 && !chainPkgs.empty())
            for (const auto& pkg : chainPkgs) stmt.bind(idx++, pkg);
    }
    for (size_t i = 0; i < schemas.size(); ++i) {
        for (auto& [pkg, name] : specificImports) {
            stmt.bind(idx++, pkg);
            stmt.bind(idx++, name);
        }
    }

    std::vector<SymbolRow> rows;
    while (stmt.step()) rows.push_back(rowFromStmt(stmt));
    keepDeclaredBefore(rows, path, pos, scopes);

    std::sort(rows.begin(), rows.end(), [](const SymbolRow& a, const SymbolRow& b) {
        if (a.scope.size() != b.scope.size())
            return a.scope.size() > b.scope.size();
        return a.name < b.name;
    });
    return rows;
}

std::vector<std::string> SymbolDatabase::scopeChain(const std::string& innermost)
{
    std::vector<std::string> chain;
    std::string cur = innermost;
    while (true) {
        chain.push_back(cur);
        auto sep = cur.rfind("::");
        if (sep == std::string::npos) break;
        cur = cur.substr(0, sep);
    }
    if (!chain.back().empty()) chain.push_back("");
    return chain;
}

const SymbolRow& SymbolDatabase::pickSameFilePreferred(const std::vector<SymbolRow>& rows,
                                                       const std::string& curPath)
{
    for (const auto& r : rows)
        if (r.filePath == curPath) return r;
    return rows.front();
}

std::vector<std::string> SymbolDatabase::baseClassChain(
    const std::string& className, const std::string& curPath) const
{
    std::vector<std::string> chain;
    std::vector<std::string> visited;
    std::string current = className;
    while (!current.empty()) {
        if (std::find(visited.begin(), visited.end(), current) != visited.end())
            break; // cycle guard: a class (in)directly naming itself as parent
        visited.push_back(current);

        // A `pkg::`/`Outer::`/`$unit::`-qualified name (plan.md §6.30 step A
        // -- userTypeName and a class's own `extends` detail keep the
        // qualifier) only matches a class declared in exactly that scope.
        // The qualifier may omit leading enclosing scopes (`Outer::Inner`
        // written inside package p is scope "p::Outer"), so a suffix match on
        // whole `::` segments is accepted too.
        std::string bare = current;
        std::optional<std::string> qualifier;
        if (auto sep = current.rfind("::"); sep != std::string::npos) {
            qualifier = current.substr(0, sep);
            bare      = current.substr(sep + 2);
            if (*qualifier == "$unit") qualifier = "";
        }
        auto inQualifier = [&](const SymbolRow& row) {
            if (!qualifier) return true;
            if (row.scope == *qualifier) return true;
            return !qualifier->empty() && row.scope.size() > qualifier->size() + 2 &&
                   row.scope.compare(row.scope.size() - qualifier->size(), qualifier->size(),
                                     *qualifier) == 0 &&
                   row.scope.compare(row.scope.size() - qualifier->size() - 2, 2, "::") == 0;
        };

        std::vector<SymbolRow> classRows, typedefRows;
        for (auto& row : findSymbolsByName(bare)) {
            if (row.kind == "Class" && inQualifier(row)) classRows.push_back(row);
            if (row.kind == "Typedef" && !isForwardTypedef(row) && inQualifier(row))
                typedefRows.push_back(row);
        }
        if (classRows.empty() && !typedefRows.empty()) {
            // A class typedef (`typedef C alias_t;`, `typedef C#(int) c_t;`):
            // continue with the aliased type, qualified with the innermost
            // scope around the typedef that declares it -- a bare `Reg` in
            // p::User's typedef is p's Reg, not another package's. The
            // visited guard covers loops.
            const SymbolRow& td = pickSameFilePreferred(typedefRows, curPath);
            current = td.detail;
            if (current.find("::") == std::string::npos) {
                const auto aliased = findSymbolsByName(current);
                for (const auto& scope : scopeChain(td.scope)) {
                    auto declares = [&](const SymbolRow& r) {
                        return (r.kind == "Class" || r.kind == "Typedef") && r.scope == scope;
                    };
                    if (std::any_of(aliased.begin(), aliased.end(), declares)) {
                        current = (scope.empty() ? "$unit" : scope) + "::" + current;
                        break;
                    }
                }
            }
            continue;
        }
        if (classRows.empty()) break; // not a known class -- stop (fail closed)

        const SymbolRow& best = pickSameFilePreferred(classRows, curPath);
        chain.push_back(best.scope.empty() ? best.name : best.scope + "::" + best.name);
        current = best.detail; // single recorded parent name, "" if none
    }
    return chain;
}
