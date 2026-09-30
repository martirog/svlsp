#include "db/symbol_database.h"
#include "compiler/parse_record.h"
#include <algorithm>
#include <optional>
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
        "INSERT INTO symbols (file_id,kind,name,line,col,parent,detail,end_line,scope) "
        "VALUES (?,?,?,?,?,?,?,?,?)");
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
           .bind(9, r.scope);
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

void SymbolDatabase::replaceDiagnostics(int64_t fileId,
                                        const std::vector<ParseError>& errors)
{
    m_db.execute("BEGIN");
    auto del = m_db.prepare("DELETE FROM diagnostics WHERE file_id = ?");
    del.bind(1, fileId);
    del.step();

    auto ins = m_db.prepare(
        "INSERT INTO diagnostics (file_id,line,col,message) VALUES (?,?,?,?)");
    for (const auto& e : errors) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, e.line)
           .bind(3, e.column)
           .bind(4, e.message);
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
        "INSERT INTO imports (file_id, pkg_name, item, is_export) VALUES (?,?,?,?)");
    for (const auto& imp : imports) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, imp.pkgName)
           .bind(3, imp.item)
           .bind(4, imp.isExport ? 1 : 0);
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
    while (stmt.step()) {
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
        rows.push_back(std::move(row));
    }
    return rows;
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
        "SELECT pkg_name, item, is_export FROM imports WHERE file_id = ?");
    stmt.bind(1, fileId);
    std::vector<ImportRow> rows;
    while (stmt.step())
        rows.push_back({stmt.columnText(0), stmt.columnText(1), stmt.columnInt(2) != 0});
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

void SymbolDatabase::appendDiagnostics(int64_t fileId, const std::vector<ParseError>& extra)
{
    m_db.execute("BEGIN");
    auto ins = m_db.prepare(
        "INSERT INTO diagnostics (file_id,line,col,message) VALUES (?,?,?,?)");
    for (const auto& e : extra) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, e.line)
           .bind(3, e.column)
           .bind(4, e.message);
        ins.step();
    }
    m_db.execute("COMMIT");
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

    for (auto& imp : importsForFileId(fid)) {
        if (!imp.isExport) continue;
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
        "SELECT line,col,message FROM diagnostics WHERE file_id = ? ORDER BY line");
    stmt.bind(1, fid);

    std::vector<DiagnosticRow> rows;
    while (stmt.step())
        rows.push_back({static_cast<int>(stmt.columnInt(0)),
                        static_cast<int>(stmt.columnInt(1)),
                        stmt.columnText(2),
                        path});
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

std::string SymbolDatabase::scopeAtPosition(
    const std::string& path, int line) const
{
    // Find the innermost scope-defining symbol (Module/Interface/Package/Class/
    // Function/Task) whose line range contains `line`.  Deepest nesting wins
    // (longest scope chain).
    auto stmt = m_db.prepare(
        "SELECT CASE WHEN s.scope = '' THEN s.name ELSE s.scope || '::' || s.name END "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? "
        "  AND s.kind IN ('Module','Interface','Package','Class','Function','Task') "
        "  AND s.line <= ? AND s.end_line >= ? "
        "ORDER BY length(s.scope) DESC, s.line DESC "
        "LIMIT 1");
    stmt.bind(1, path).bind(2, line).bind(3, line);
    if (stmt.step()) return stmt.columnText(0);
    return {};
}

std::string SymbolDatabase::scopeKindAtPosition(
    const std::string& path, int line) const
{
    // Same shape as scopeAtPosition (including its existing kind list, which
    // doesn't track 'Program' as a scope kind either) but selects the kind
    // of the innermost scope-defining symbol instead of its full name path.
    auto stmt = m_db.prepare(
        "SELECT s.kind "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? "
        "  AND s.kind IN ('Module','Interface','Package','Class','Function','Task') "
        "  AND s.line <= ? AND s.end_line >= ? "
        "ORDER BY length(s.scope) DESC, s.line DESC "
        "LIMIT 1");
    stmt.bind(1, path).bind(2, line).bind(3, line);
    if (stmt.step()) return stmt.columnText(0);
    return {};
}

std::string SymbolDatabase::enclosingClassNameAt(
    const std::string& path, int line) const
{
    // Same shape as scopeAtPosition/scopeKindAtPosition, but filtered to
    // Class specifically -- finds the nearest enclosing class even when the
    // innermost scope at `line` is one of its Function/Task members. Falls
    // back to the scope chain for an out-of-class method body (below).
    auto stmt = m_db.prepare(
        "SELECT s.name "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? "
        "  AND s.kind = 'Class' "
        "  AND s.line <= ? AND s.end_line >= ? "
        "ORDER BY length(s.scope) DESC, s.line DESC "
        "LIMIT 1");
    stmt.bind(1, path).bind(2, line).bind(3, line);
    if (stmt.step()) return stmt.columnText(0);

    // An out-of-class method body (plan.md §6.30 step D) lies outside its
    // class's line range but is scoped under it: the innermost scope is
    // "<class scope>::m", so find the nearest class on the scope chain.
    std::string cur = scopeAtPosition(path, line);
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

std::vector<SymbolRow> SymbolDatabase::findSymbolsVisibleAt(
    const std::string& path, int line) const
{
    // The scope chain from innermost outward, ending with "".
    const std::vector<std::string> scopes = scopeChain(scopeAtPosition(path, line));

    // Load imports for this file: wildcard (`import pkg::*`) and specific
    // (`import pkg::Name`).  Wildcards extend the cross-file scope search;
    // specific imports are fetched with an extra name-filtered UNION ALL.
    int64_t fid = fileIdFor(path);
    std::vector<std::string> wildcardPkgs;
    std::vector<std::pair<std::string, std::string>> specificImports; // {pkg, name}
    if (fid >= 0) {
        for (auto& imp : importsForFileId(fid)) {
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

    // Part 1 placeholders: one ? per local scope.
    std::string localPh;
    for (size_t i = 0; i < scopes.size(); ++i) { if (i) localPh += ','; localPh += '?'; }

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

    // Part 2: top-level symbols from every *other* file (this file's own are
    // already in Part 1's chain, which always ends with ""), plus every
    // wildcard-imported package's members from *any* file -- including this
    // one, since a package can be declared and imported in the same file
    // (plan.md §6.30 step 1). The one exclusion keeps a wildcard package
    // that is also on the cursor's own scope chain (the cursor is inside the
    // imported package itself) from returning its rows twice.
    std::string part2Cond = "(f.path != ? AND s.scope = '')";
    if (!wildcardPkgs.empty())
        part2Cond += " OR (s.scope IN (" + wildPh + ") AND NOT (f.path = ? AND s.scope IN (" +
                     localPh + ")))";
    for (const auto& schema : schemas)
        sql += " UNION ALL "
               "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
               "FROM " + schema + "symbols s JOIN " + schema + "files f ON f.id = s.file_id "
               "WHERE " + part2Cond;

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
    for (size_t i = 0; i < schemas.size(); ++i) {
        stmt.bind(idx++, path);
        if (wildcardPkgs.empty()) continue;
        for (const auto& pkg : wildcardPkgs) stmt.bind(idx++, pkg);
        stmt.bind(idx++, path);
        for (const auto& sc : scopes) stmt.bind(idx++, sc);
    }
    for (size_t i = 0; i < schemas.size(); ++i) {
        for (auto& [pkg, name] : specificImports) {
            stmt.bind(idx++, pkg);
            stmt.bind(idx++, name);
        }
    }

    std::vector<SymbolRow> rows;
    while (stmt.step()) rows.push_back(rowFromStmt(stmt));

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
            if (row.kind == "Typedef" && inQualifier(row)) typedefRows.push_back(row);
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

std::optional<SymbolRow> SymbolDatabase::resolveMethod(
    const std::string& className, const std::string& methodName,
    const std::string& curPath) const
{
    for (const auto& scope : baseClassChain(className, curPath)) {
        std::vector<SymbolRow> candidates;
        for (auto& row : findSymbolsInScope(scope))
            if ((row.kind == "Function" || row.kind == "Task") && row.name == methodName)
                candidates.push_back(row);
        if (!candidates.empty())
            return pickSameFilePreferred(candidates, curPath);
    }
    return std::nullopt;
}
