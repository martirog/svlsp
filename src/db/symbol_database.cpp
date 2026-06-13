#include "db/symbol_database.h"
#include "compiler/parse_record.h"
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
    }
    return "Unknown";
}

SymbolDatabase::SymbolDatabase(Database& db)
    : m_db{db}
{}

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
    m_db.execute("COMMIT");
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
    auto stmt = m_db.prepare(
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE s.name = ? ORDER BY f.path, s.line");
    stmt.bind(1, name);

    std::vector<SymbolRow> rows;
    while (stmt.step()) {
        rows.push_back({stmt.columnInt(0),
                        stmt.columnText(1),
                        stmt.columnText(2),
                        static_cast<int>(stmt.columnInt(3)),
                        static_cast<int>(stmt.columnInt(4)),
                        stmt.columnText(5),
                        stmt.columnText(6),
                        stmt.columnText(9),
                        static_cast<int>(stmt.columnInt(7)),
                        stmt.columnText(8)});
    }
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

std::vector<SymbolRow> SymbolDatabase::findSymbolsInScope(
    const std::string& scope) const
{
    auto stmt = m_db.prepare(
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE s.scope = ? ORDER BY s.name");
    stmt.bind(1, scope);
    std::vector<SymbolRow> rows;
    while (stmt.step()) rows.push_back(rowFromStmt(stmt));
    return rows;
}

std::vector<SymbolRow> SymbolDatabase::findSymbolsByNamePrefix(
    const std::string& prefix) const
{
    auto stmt = m_db.prepare(
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE s.name LIKE ? ESCAPE '\\' ORDER BY s.name, f.path");
    stmt.bind(1, prefix + "%");
    std::vector<SymbolRow> rows;
    while (stmt.step()) rows.push_back(rowFromStmt(stmt));
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

std::vector<SymbolRow> SymbolDatabase::findSymbolsVisibleAt(
    const std::string& path, int line) const
{
    // Build the scope chain from innermost outward, ending with "".
    // e.g. "MyModule::MyClass::myFunc" → ["MyModule::MyClass::myFunc",
    //                                      "MyModule::MyClass", "MyModule", ""]
    std::string inner = scopeAtPosition(path, line);
    std::vector<std::string> scopes;
    std::string cur = inner;
    while (true) {
        scopes.push_back(cur);
        auto sep = cur.rfind("::");
        if (sep == std::string::npos) break;
        cur = cur.substr(0, sep);
    }
    if (scopes.empty() || !scopes.back().empty())
        scopes.push_back("");

    // Part 1: file-local symbols in any scope in the chain.
    // Part 2: top-level symbols (scope = "") from all other files.
    std::string placeholders;
    for (size_t i = 0; i < scopes.size(); ++i) {
        if (i) placeholders += ',';
        placeholders += '?';
    }
    std::string sql =
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path = ? AND s.scope IN (" + placeholders + ") "
        "UNION ALL "
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,s.end_line,s.scope,f.path "
        "FROM symbols s JOIN files f ON f.id = s.file_id "
        "WHERE f.path != ? AND s.scope = '' "
        "ORDER BY length(s.scope) DESC, s.name";

    auto stmt = m_db.prepare(sql);
    int idx = 1;
    stmt.bind(idx++, path);
    for (const auto& sc : scopes) stmt.bind(idx++, sc);
    stmt.bind(idx,   path);

    std::vector<SymbolRow> rows;
    while (stmt.step()) rows.push_back(rowFromStmt(stmt));
    return rows;
}
