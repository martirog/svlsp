#include "lsp/completion.h"
#include "lsp/symbol_utils.h"
#include "lsp/fuzzy_match.h"
#include "lsp/sv_keywords.h"
#include "lsp/sv_builtin_methods.h"
#include <algorithm>
#include <optional>
#include <cstdio>
#include <set>
#include <unordered_set>
#include <vector>

namespace {

// A completion candidate, independent of its source (a DB symbol row or a
// synthetic SV keyword) -- lets keyword and symbol candidates share one
// fuzzy-score/sort/build pipeline.
struct Candidate {
    std::string              name;
    lsp::CompletionItemKind  kind;
    std::string              detail; // empty = omit item.detail
    std::optional<SymbolRow> row{};  // the DB symbol, for resolve's `data`
};

// One candidate per (name, kind): an extern method's prototype and its
// out-of-class body are both visible inside the class (plan.md §6.30 step
// D), and a shadowed name would otherwise be offered once per scope.
// Constructor rows are skipped: `obj.new` isn't legal, and the `new`
// keyword is already offered where it is.
std::vector<Candidate> candidatesFromRows(const std::vector<SymbolRow>& rows)
{
    std::vector<Candidate> out;
    out.reserve(rows.size());
    std::set<std::pair<std::string, std::string>> seen;
    for (auto& row : rows)
        if (!(row.kind == "Function" && row.name == "new") &&
            seen.emplace(row.name, row.kind).second)
            out.push_back({row.name, completionKindFor(row.kind), row.detail, row});
    return out;
}

// Legal keywords at `scopeKind` (a SymbolDatabase::scopeKindAtPosition()
// result) as candidates, for merging into the non-dot completion pool.
std::vector<Candidate> candidatesFromKeywords(const std::string& scopeKind)
{
    const unsigned bit = contextBitFor(scopeKind);
    std::vector<Candidate> out;
    for (const auto& kw : SV_KEYWORDS) {
        if (kw.contexts & bit)
            out.push_back({std::string(kw.text), lsp::CompletionItemKind::Keyword, ""});
    }
    return out;
}

std::vector<Candidate> candidatesFromMethods(std::span<const BuiltinMethod> methods)
{
    std::vector<Candidate> out;
    out.reserve(methods.size());
    for (auto& m : methods)
        out.push_back({std::string(m.name), m.kind, std::string(m.detail)});
    return out;
}

// Filters and (when `fuzzyEnabled`) ranks `candidates` against `prefix`,
// then builds the LSP item list. With no prefix every candidate stays in,
// unranked, regardless of `fuzzyEnabled`. Returns nullptr if `candidates`
// is empty, or if every candidate fails to match a non-empty prefix.
//
// `fuzzyEnabled` (plan.md §6.11): true (default, today's unchanged
// behavior) fuzzy-filters via `fuzzyScore` (subsequence match, typo/skip
// tolerant) and sorts by descending score. false restores the pre-fuzzy
// semantics §3.6 documents as the prior state: a strict, case-sensitive
// prefix filter (`compare(0, prefix.size(), prefix) != 0` to reject), no
// re-sort (candidate/DB order preserved), no `sortText` assigned -- a real
// fuzzy match can legitimately score low, which isn't the same thing as
// "no ranking should apply", so this is a distinct code path rather than a
// special-cased score threshold.
lsp::TextDocument_CompletionResult buildCompletionItems(
    const std::vector<Candidate>& candidates, const std::string& prefix, bool fuzzyEnabled)
{
    if (candidates.empty()) return nullptr;

    struct Scored { const Candidate* candidate; int score; };
    std::vector<Scored> scored;
    scored.reserve(candidates.size());
    for (auto& c : candidates) {
        int score = 0;
        if (!prefix.empty()) {
            if (fuzzyEnabled) {
                auto s = fuzzyScore(c.name, prefix);
                if (!s) continue;
                score = *s;
            } else if (c.name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
        }
        scored.push_back({&c, score});
    }
    if (scored.empty()) return nullptr;

    if (fuzzyEnabled) {
        std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.candidate->name < b.candidate->name;
        });
    }

    lsp::Array<lsp::CompletionItem> items;
    items.reserve(scored.size());
    for (size_t i = 0; i < scored.size(); ++i) {
        const auto& c = *scored[i].candidate;

        lsp::CompletionItem item;
        item.label = c.name;
        item.kind  = c.kind;
        if (!c.detail.empty())
            item.detail = c.detail;
        if (c.row) {
            lsp::json::Object data;
            data["path"]  = lsp::json::String(c.row->filePath);
            data["line"]  = lsp::json::Integer(c.row->line);
            data["col"]   = lsp::json::Integer(c.row->col);
            data["kind"]  = lsp::json::String(c.row->kind);
            data["scope"] = lsp::json::String(c.row->scope);
            item.data = lsp::json::Value(std::move(data));
        }
        if (fuzzyEnabled && !prefix.empty()) {
            // Zero-padded rank so clients that re-sort by sortText (rather
            // than trusting response order) preserve our fuzzy ranking.
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%05zu", i);
            item.sortText = std::string(buf);
        }
        items.push_back(std::move(item));
    }

    return items;
}

// Builds the candidate list for whatever a dot-completion chain resolved
// to -- shared by every chain length (a 1-segment chain reproduces
// §6.10/§6.13's original single-hop behavior exactly; this is the only
// place that logic lives now). Guards against `detail.empty()` internally:
// "" is not "unknown type" in this schema, it's the literal top-level
// scope value every top-level symbol is stored under, so
// findSymbolsInScope("") would wrongly return the whole project's
// top-level symbols instead of nothing (plan.md §6.14's own documented
// landmine).
std::vector<Candidate> candidatesForResolvedType(SymbolDatabase& db, const std::string& detail,
                                                  const std::string& curPath,
                                                  const std::vector<std::string>& cursorClasses)
{
    if (detail.empty()) return {};

    // Built-in container/type methods (plan.md §6.13, §6.15): queues,
    // associative/dynamic/fixed-size arrays, mailbox, semaphore, process,
    // string, event -- none of these are ParseRecordKinds, so
    // findSymbolsInScope would return nothing for them. Resolved from a
    // static table alone, no DB call. Dispatches on the outermost layer
    // only -- a container's own method set doesn't depend on its element
    // type (§6.15's "q." on "MyClass q[$]" still means the queue's own
    // methods, not MyClass's).
    if (auto methods = builtinMethodsFor(firstTypeLayer(detail)); !methods.empty())
        return candidatesFromMethods(methods);

    // Otherwise, a real DB Class/Interface/whatever scope lookup, unioned
    // across `detail`'s own class and every ancestor via `extends`
    // (baseClassChain, plan.md §6.26 -- also handles a class declared
    // inside a package or nested inside another class, same as
    // qualifiedClassScope's own §6.17 handling, since baseClassChain builds
    // each qualified scope name the same way). A non-empty chain here
    // already proves `detail` names a genuine Class, so it doubles as the
    // gate for unioning in the randomize-family methods below -- no
    // separate findSymbolsByName scan needed for that.
    auto chain = db.baseClassChain(detail, curPath);
    if (chain.empty()) return {};

    // Access (LRM 8.18): a `local` member is reachable only from inside its
    // own class, a `protected` one from its class or a subclass. A nested
    // class has its containing class's access rights (LRM 8.23), so every
    // enclosing class counts. Applied after the first-name-wins dedup: if
    // the nearest `x` up the chain is inaccessible, `obj.x` names it all
    // the same, so a farther `x` isn't offered in its place.
    std::unordered_set<std::string> protectedVisible;
    for (const auto& cls : cursorClasses)
        for (auto& scope : db.baseClassChain(cls.find("::") == std::string::npos
                                                 ? "$unit::" + cls : cls, curPath))
            protectedVisible.insert(std::move(scope));
    auto accessible = [&](const std::string& scope, const std::string& access) {
        if (access == "local")
            return std::find(cursorClasses.begin(), cursorClasses.end(), scope) !=
                   cursorClasses.end();
        if (access == "protected") return protectedVisible.count(scope) > 0;
        return true;
    };

    std::vector<SymbolRow> memberRows;
    std::unordered_set<std::string> seenNames;
    for (auto& scope : chain) {
        const auto access = db.memberAccess(scope);
        for (auto& row : db.findSymbolsInScope(scope)) {
            if (!seenNames.insert(row.name).second) continue;
            const auto it = access.find(row.name);
            if (it == access.end() || accessible(scope, it->second)) memberRows.push_back(row);
        }
    }
    auto candidates = candidatesFromRows(memberRows);

    // Union the randomize-family methods the LRM implicitly grants every
    // class. A user class that declares its own `randomize` override
    // keeps the real (DB) one -- skip the synthetic entry on a name
    // collision rather than duplicating it (seenNames: an inaccessible
    // override still replaces the built-in).
    for (auto& m : RANDOMIZE_METHODS)
        if (!seenNames.count(std::string(m.name)))
            candidates.push_back({std::string(m.name), m.kind, std::string(m.detail)});

    return candidates;
}

} // namespace

lsp::TextDocument_CompletionResult CompletionProvider::getCompletion(
    const lsp::CompletionParams& params, SymbolDatabase& db, const std::string& docText,
    bool fuzzyEnabled)
{
    const std::string path{params.textDocument.uri.path()};
    // LSP position is 0-based; scopeAtPosition uses 1-based lines.
    const int line1 = static_cast<int>(params.position.line) + 1;

    // Typing inside a comment or a string literal is prose, not code.
    if (auto offset = offsetForPosition(docText, params.position.line, params.position.character);
        offset && insideCommentOrString(docText, *offset))
        return nullptr;

    if (auto dot = dotCompletionContext(docText, params.position.line, params.position.character)) {
        auto resolved = resolveChain(db, path, line1, dot->segments);
        if (!resolved) return nullptr;
        return buildCompletionItems(
            candidatesForResolvedType(db, *resolved, path, db.enclosingClassScopesAt(path, line1)),
            dot->prefix, fuzzyEnabled);
    }

    const std::string prefix = wordAtPosition(docText,
                                              params.position.line,
                                              params.position.character);

    // Merge DB symbol rows with whatever SV keywords are legal at this
    // scope (plan.md §6.9) before scoring/emptiness-checking, so a file
    // with zero DB symbols (e.g. a brand-new buffer) still offers top-level
    // keywords instead of returning null.
    // Inside a class (or an out-of-class method body), its ancestors'
    // members are in scope too, except the ones they declare `local`.
    // Appended after the visible set, so the (name, kind) dedup lets a
    // class's own member shadow an inherited one.
    auto rows = db.findSymbolsVisibleAt(path, line1);
    if (const std::string cls = db.enclosingClassNameAt(path, line1); !cls.empty()) {
        const auto chain = db.baseClassChain(cls, path);
        for (size_t i = 1; i < chain.size(); ++i) {
            const auto access = db.memberAccess(chain[i]);
            for (auto& row : db.findSymbolsInScope(chain[i])) {
                const auto it = access.find(row.name);
                if (it == access.end() || it->second != "local") rows.push_back(std::move(row));
            }
        }
    }
    auto candidates = candidatesFromRows(rows);
    auto keywords    = candidatesFromKeywords(db.scopeKindAtPosition(path, line1));
    candidates.insert(candidates.end(),
                       std::make_move_iterator(keywords.begin()),
                       std::make_move_iterator(keywords.end()));
    return buildCompletionItems(candidates, prefix, fuzzyEnabled);
}

lsp::CompletionItem CompletionProvider::resolve(lsp::CompletionItem item, SymbolDatabase& db)
{
    if (!item.data || !item.data->isObject()) return item;
    const auto& data = item.data->object();
    const auto* path  = data.find("path");
    const auto* line  = data.find("line");
    const auto* col   = data.find("col");
    const auto* kind  = data.find("kind");
    const auto* scope = data.find("scope");
    if (!path || !path->isString() || !line || !line->isInteger() || !col || !col->isInteger() ||
        !kind || !kind->isString() || !scope || !scope->isString())
        return item;

    SymbolRow row{};
    row.name     = item.label;
    row.kind     = kind->string();
    row.filePath = path->string();
    row.line     = static_cast<int>(line->integer());
    row.col      = static_cast<int>(col->integer());
    row.scope    = scope->string();
    if (std::string doc = symbolDoc(db, row); !doc.empty())
        item.documentation = lsp::MarkupContent{lsp::MarkupKind::Markdown, docMarkdown(doc)};
    return item;
}
