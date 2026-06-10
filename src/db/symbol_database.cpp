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
        "INSERT INTO symbols (file_id,kind,name,line,col,parent,detail) "
        "VALUES (?,?,?,?,?,?,?)");
    for (const auto& r : records) {
        ins.reset();
        ins.bind(1, fileId)
           .bind(2, kindStr(r.kind))
           .bind(3, r.name)
           .bind(4, r.line)
           .bind(5, r.column)
           .bind(6, r.parent)
           .bind(7, r.detail);
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
        "SELECT id,kind,name,line,col,parent,detail FROM symbols "
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
                        path});
    }
    return rows;
}

std::vector<SymbolRow> SymbolDatabase::findSymbolsByName(
    const std::string& name) const
{
    auto stmt = m_db.prepare(
        "SELECT s.id,s.kind,s.name,s.line,s.col,s.parent,s.detail,f.path "
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
                        stmt.columnText(7)});
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
