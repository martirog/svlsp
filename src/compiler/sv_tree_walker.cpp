#include "compiler/sv_tree_walker.h"
#include "SvBaseListener.h"
#include "SvLexer.h"
#include "SvParser.h"
#include <antlr4-runtime.h>
#include <optional>

// ---------------------------------------------------------------------------
// Source map translation helper
// ---------------------------------------------------------------------------

// Translates a 1-based compiled line number through the source map.
// Returns the original {file, line}; file is empty when the position belongs
// to the primary compiled file (same-file, no attribution change needed).
static std::pair<std::string, int> translateLine(int compiledLine,
                                                  const std::vector<SourceLine>& map)
{
    int idx = compiledLine - 1;
    if (idx >= 0 && idx < static_cast<int>(map.size()))
        return {map[idx].file, map[idx].line};
    return {"", compiledLine};
}

// Translates a 0-based column on `compiledLine` (1-based) through that line's
// colShifts, undoing any column drift introduced by mid-line macro expansion
// earlier on the same line. Columns before the first breakpoint, or lines
// with no breakpoints at all, pass through unchanged.
static int translateColumn(int compiledLine, int compiledCol,
                            const std::vector<SourceLine>& map)
{
    int idx = compiledLine - 1;
    if (idx < 0 || idx >= static_cast<int>(map.size())) return compiledCol;
    int delta = 0;
    for (const auto& shift : map[idx].colShifts) {
        if (compiledCol >= shift.outputCol) delta = shift.delta;
        else break;
    }
    return compiledCol + delta;
}

// Extracts the bare identifier for a class/interface/struct/typedef-typed
// declaration's data_type_or_implicit (e.g. "MyClass" from "MyClass foo;"),
// or "" for a built-in type (logic, int, ...) or any other data_type
// alternative with no member scope to resolve into (struct/enum literal,
// string, event, ...). Powers dot-completion's object-type resolution
// (plan.md §6.10) via Signal/Parameter ParseRecord::detail.
// Ignores any trailing packed_dimension/parameter_value_assignment: `foo` is
// what SymbolDatabase::findSymbolsInScope keys a class/interface's member
// scope by, not `foo [7:0]` or `foo#(...)`.
// "pkg" for `pkg::`, "$unit" for `$unit::`, "" when there's no package scope.
static std::string packageScopeName(SvParser::Package_scopeContext* ps)
{
    if (!ps) return "";
    if (auto* pi = ps->package_identifier())
        return pi->IDENTIFIER() ? pi->IDENTIFIER()->getText() : "";
    return "$unit";
}

// The `::`-joined name path of a class_type -- `[pkg::]C[::Nested...]` --
// with every parameter value assignment (`#(...)`) dropped: only the name
// path matters for resolving which class is meant (plan.md §6.30 step A).
// "" if the class_type has no identifier.
static std::string classTypeName(SvParser::Class_typeContext* ct)
{
    if (!ct) return "";
    auto* psci = ct->ps_class_identifier();
    if (!psci || !psci->class_identifier() || !psci->class_identifier()->IDENTIFIER())
        return "";
    std::string name;
    if (auto pkg = packageScopeName(psci->package_scope()); !pkg.empty())
        name = pkg + "::";
    name += psci->class_identifier()->IDENTIFIER()->getText();
    for (auto* nested : ct->class_identifier())
        if (nested->IDENTIFIER()) name += "::" + nested->IDENTIFIER()->getText();
    return name;
}

// The identifier of a data_type_or_implicit that is nothing but one
// unqualified, unparameterized name (`b`, or `b [4]` read as packed
// dimensions), else nullptr. In a tf_port_item with no port_identifier this
// is the comma-shorthand ambiguity: `(input int a, b)` parses `b` as the
// *type* of an unnamed argument.
static antlr4::tree::TerminalNode* bareTypeIdentifier(SvParser::Data_type_or_implicitContext* dtoi)
{
    auto* dt = dtoi ? dtoi->data_type() : nullptr;
    if (!dt) return nullptr;
    if (auto* ti = dt->type_identifier())
        return (dt->class_scope() || dt->package_scope()) ? nullptr : ti->IDENTIFIER();
    if (auto* ct = dt->class_type()) {
        auto* psci = ct->ps_class_identifier();
        if (!psci || psci->package_scope() || !ct->parameter_value_assignment().empty() ||
            !ct->class_identifier().empty() || !psci->class_identifier())
            return nullptr;
        return psci->class_identifier()->IDENTIFIER();
    }
    if (auto* cg = dt->ps_covergroup_identifier())
        return (cg->package_scope() || !cg->covergroup_identifier())
                   ? nullptr : cg->covergroup_identifier()->IDENTIFIER();
    return nullptr;
}

// The declared user type of a data_type, *with* any `pkg::`/`Class::`/
// `$unit::` qualifier the source wrote (plan.md §6.30 step A -- previously
// only the bare trailing identifier was kept, so pkg_a::Item and
// pkg_b::Item were indistinguishable). "" for every built-in type and
// anything that isn't a named type reference.
static std::string dataTypeName(SvParser::Data_typeContext* dt)
{
    if (!dt) return "";
    if (auto* ti = dt->type_identifier()) {
        if (!ti->IDENTIFIER()) return "";
        std::string qualifier;
        if (auto* cs = dt->class_scope())
            qualifier = classTypeName(cs->class_type());
        else
            qualifier = packageScopeName(dt->package_scope());
        const std::string name = ti->IDENTIFIER()->getText();
        return qualifier.empty() ? name : qualifier + "::" + name;
    }
    if (auto* ct = dt->class_type())
        return classTypeName(ct);
    return "";
}

// dataTypeName of a data_type_or_implicit ("" for an implicit type).
static std::string userTypeName(SvParser::Data_type_or_implicitContext* dtoi)
{
    return dtoi ? dataTypeName(dtoi->data_type()) : "";
}

// True for a data_declaration that is really an assignment statement the
// grammar misparsed: its first alternative accepts an implicit type, so a
// block's leading `x = expr;` matches it as a declaration of `x`. The LRM
// only allows an implicit-typed data declaration with `var`, so an implicit
// type with no `var`, no signing and no packed dimension can't be a real
// declaration (plan.md §6.30 step A -- these phantom locals shadowed the
// real declarations in every scoped lookup).
static bool isMisparsedAssignment(SvParser::Data_declarationContext* ctx)
{
    auto* dtoi = ctx->data_type_or_implicit();
    if (!dtoi || dtoi->data_type()) return false;
    if (auto* imp = dtoi->implicit_data_type())
        if (imp->signing() || !imp->packed_dimension().empty()) return false;
    for (auto* child : ctx->children)
        if (auto* term = dynamic_cast<antlr4::tree::TerminalNode*>(child))
            if (term->getText() == "var") return false;
    return true;
}

// Name of the first variable a variable_decl_assignment list declares, or
// "" -- the owner a struct/union type declared inline in that declaration
// belongs to (`struct {...} s, t;` scopes its members under `s`).
static std::string firstVariableName(SvParser::List_of_variable_decl_assignmentsContext* list)
{
    if (!list || list->variable_decl_assignment().empty()) return "";
    auto* vi = list->variable_decl_assignment(0)->variable_identifier();
    return vi && vi->IDENTIFIER() ? vi->IDENTIFIER()->getText() : "";
}

// The `::`-joined owner path of a struct/union data_type (plan.md §6.30
// step C): the typedef name for `typedef struct {...} name;`, the variable
// for `struct {...} v;`, and `<outer owner>::<member>` for a struct nested
// as a member of another. std::nullopt for any other position (a port or
// parameter type, a cast), where the members aren't recorded.
static std::optional<std::string> structOwnerPath(SvParser::Data_typeContext* dt)
{
    if (!dt) return std::nullopt;
    auto* parent = dt->parent;
    if (auto* td = dynamic_cast<SvParser::Type_declarationContext*>(parent)) {
        if (td->type_identifier().empty() || !td->type_identifier(0)->IDENTIFIER())
            return std::nullopt;
        return td->type_identifier(0)->IDENTIFIER()->getText();
    }
    if (auto* dtoi = dynamic_cast<SvParser::Data_type_or_implicitContext*>(parent)) {
        auto* decl = dynamic_cast<SvParser::Data_declarationContext*>(dtoi->parent);
        if (!decl) return std::nullopt;
        auto name = firstVariableName(decl->list_of_variable_decl_assignments());
        if (name.empty()) return std::nullopt;
        return name;
    }
    if (auto* dtov = dynamic_cast<SvParser::Data_type_or_voidContext*>(parent)) {
        auto* member = dynamic_cast<SvParser::Struct_union_memberContext*>(dtov->parent);
        if (!member) return std::nullopt;
        auto outer = structOwnerPath(dynamic_cast<SvParser::Data_typeContext*>(member->parent));
        auto name  = firstVariableName(member->list_of_variable_decl_assignments());
        if (!outer || name.empty()) return std::nullopt;
        return *outer + "::" + name;
    }
    return std::nullopt;
}

// Detail tag for a bare-literal data_type alternative that has its own
// implicit built-in methods but no ParseRecordKind/DB scope (string, event)
// -- see src/lsp/sv_builtin_methods.h for the method tables these tags
// select. userTypeName() above can't see these: 'string'/'event' are bare
// keyword alternatives of data_type (grammar/Sv.g4) with no dedicated
// type_identifier()/class_type() accessor, unlike a real class/interface
// reference. Checking getText() is reliable specifically because these two
// alternatives have exactly one terminal child and no packed_dimension, so
// it yields the bare keyword with nothing else attached. "" for every other
// data_type alternative (built-in scalar types need no tag).
static std::string builtinBareTypeTag(SvParser::Data_type_or_implicitContext* dtoi)
{
    if (!dtoi) return "";
    auto* dt = dtoi->data_type();
    if (!dt) return "";
    const std::string text = dt->getText();
    if (text == "string") return CONTAINER_STRING;
    if (text == "event")  return CONTAINER_EVENT;
    return "";
}

// Ordered list of container-dimension tags for a queue/associative-array/
// dynamic-array/fixed-size-array declarator, from its own
// variable_dimension() list (plan.md §6.13, extended to the full list by
// §6.15) -- container-ness lives on the specific declarator, not the
// shared data_type_or_implicit (e.g. "int a[$], b;" tags only `a`).
// Outermost/leftmost dimension first, matching declaration order (e.g.
// "arr[4][$]" -- a fixed array of queues -- yields
// [CONTAINER_FIXED_ARRAY, CONTAINER_QUEUE]): §6.15's whole-chain indexed-
// access resolution peels these off the front one at a time as each `[...]`
// in `arr[i][j]` is consumed. Empty when `vda` has no container/array
// dimension at all.
static std::vector<std::string> containerDimensionTags(SvParser::Variable_decl_assignmentContext* vda)
{
    std::vector<std::string> tags;
    for (auto* dim : vda->variable_dimension()) {
        if (dim->queue_dimension())            tags.push_back(CONTAINER_QUEUE);
        else if (dim->associative_dimension()) tags.push_back(CONTAINER_ASSOC);
        else if (dim->unsized_dimension())     tags.push_back(CONTAINER_DYNAMIC_ARRAY);
        else if (dim->unpacked_dimension())    tags.push_back(CONTAINER_FIXED_ARRAY);
    }
    return tags;
}

// Ordered argument slots for one bare call site's list_of_arguments
// (plan.md §6.23). Grammar:
//   expression? (',' expression?)* (',' '.' IDENTIFIER '(' expression? ')')*
// | '.' IDENTIFIER '(' expression? ')' (',' '.' IDENTIFIER '(' expression? ')')*
// ANTLR flattens both alternatives directly onto ctx's own children (no
// labeled subrule wraps a slot) -- the ordered list of ExpressionContext*/
// literal-token children *is* the slot sequence, read left to right. A
// positional slot's own expression is either present (an ExpressionContext
// child) or elided (nothing between two commas -- the `expression?`
// alternative simply produced no node); a named slot always has an
// IDENTIFIER child bracketed by its own '(' ')' pair -- distinct from the
// call's own enclosing parens, which belong to the parent Tf_callContext,
// not this rule, so they never confuse the scan below.
static std::vector<CallArgSlot> extractArgSlots(SvParser::List_of_argumentsContext* ctx)
{
    std::vector<CallArgSlot> slots;
    if (!ctx) return slots;

    bool expectingSlotStart = true; // true right after '(' or a comma
    size_t n = ctx->children.size();
    for (size_t i = 0; i < n; ) {
        auto* child = ctx->children[i];
        if (dynamic_cast<SvParser::ExpressionContext*>(child)) {
            slots.push_back({CallArgSlot::Kind::Positional});
            expectingSlotStart = false;
            ++i;
            continue;
        }
        auto* term = dynamic_cast<antlr4::tree::TerminalNode*>(child);
        if (!term) { ++i; continue; } // unreachable for this grammar rule
        const std::string text = term->getText();
        if (text == ",") {
            // A comma reached while still expecting a fresh slot (i.e. no
            // expression consumed one since the last comma/'(' ) means the
            // slot between them was elided.
            if (expectingSlotStart) slots.push_back({CallArgSlot::Kind::Elided});
            expectingSlotStart = true;
            ++i;
            continue;
        }
        if (text == ".") {
            // Named slot: '.' IDENTIFIER '(' expression? ')' -- consumed as
            // one unit so its own '(' ')' can never be mistaken for a
            // positional slot boundary by this same loop.
            ++i; // '.'
            std::string name;
            if (i < n) {
                if (auto* idTerm = dynamic_cast<antlr4::tree::TerminalNode*>(ctx->children[i])) {
                    name = idTerm->getText();
                    ++i;
                }
            }
            if (i < n) ++i; // '('
            if (i < n && dynamic_cast<SvParser::ExpressionContext*>(ctx->children[i])) ++i;
            if (i < n) ++i; // ')'
            slots.push_back({CallArgSlot::Kind::Named, name});
            expectingSlotStart = false;
            continue;
        }
        ++i; // defensive: '(' / ')' shouldn't reach here at this loop's top level
    }
    // A dangling trailing comma with nothing after it (`foo(a,)`) elides the
    // final positional slot -- the loop above only catches an elided slot
    // that has something *after* it (a comma at both ends); this is the
    // "nothing after" case.
    if (expectingSlotStart && n > 0)
        slots.push_back({CallArgSlot::Kind::Elided});

    return slots;
}

// Extracts the immediate scope name from a `::`-qualified call identifier's
// own verbatim text (plan.md §6.26), or "" if `idText` has no `::` at all.
// "Immediate" means the segment directly before the *last* `::` -- e.g.
// "type_id" (not "T") for the doubly-qualified `T::type_id::create` idiom
// (`class_scope tf_identifier`, see the Sv.g4 grammar-quirks table), since
// that is the name the call is actually being resolved against. `idText`
// comes from the token stream (m_tokens->getText), not ctx->getText(), so
// stray inter-token whitespace around a `::` (`Class :: method`) is
// possible and trimmed here rather than assumed absent.
static std::string extractCalleeScope(const std::string& idText)
{
    size_t lastSep = idText.rfind("::");
    if (lastSep == std::string::npos) return "";
    std::string scope = idText.substr(0, lastSep);
    size_t prevSep = scope.rfind("::");
    if (prevSep != std::string::npos) scope = scope.substr(prevSep + 2);

    size_t start = scope.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = scope.find_last_not_of(" \t\r\n");
    return scope.substr(start, end - start + 1);
}

// ---------------------------------------------------------------------------
// SvErrorListener — collects ANTLR4 syntax errors into ParseError[]
// ---------------------------------------------------------------------------

class SvErrorListener : public antlr4::BaseErrorListener {
public:
    explicit SvErrorListener(const std::vector<SourceLine>& sourceMap)
        : m_sourceMap(sourceMap) {}

    void syntaxError(antlr4::Recognizer* /*recognizer*/,
                     antlr4::Token* /*offendingSymbol*/,
                     size_t line, size_t charPositionInLine,
                     const std::string& msg,
                     std::exception_ptr /*e*/) override {
        auto [file, origLine] = translateLine(static_cast<int>(line), m_sourceMap);
        int origCol = translateColumn(static_cast<int>(line),
                                       static_cast<int>(charPositionInLine), m_sourceMap);
        m_errors.push_back({origLine, origCol, msg, file});
    }

    const std::vector<ParseError>& errors() const { return m_errors; }

private:
    const std::vector<SourceLine>& m_sourceMap;
    std::vector<ParseError> m_errors;
};

// ---------------------------------------------------------------------------
// SvRecordListener — internal ANTLR4 listener that fills ParseRecord[]
// ---------------------------------------------------------------------------

class SvRecordListener : public SvBaseListener {
public:
    // `tokens` is needed only for verbatim (whitespace-preserving) source
    // text of a function/task parameter's type/default-value (plan.md
    // §6.22 follow-up) -- ctx->getText() strips inter-token whitespace
    // entirely ("input logic [7:0] a" -> "inputlogic[7:0]a"), fine for the
    // short single-token-ish `detail` strings populated elsewhere but not
    // for a multi-token parameter list a person reads character-by-character
    // in a signature-help popup.
    SvRecordListener(const std::vector<SourceLine>& sourceMap,
                      antlr4::CommonTokenStream* tokens)
        : m_sourceMap(sourceMap), m_tokens(tokens) {}

    const std::vector<ParseRecord>&  records() const { return m_records; }
    const std::vector<ImportRecord>& imports() const { return m_imports; }
    const std::vector<InstantiationRecord>& instantiations() const { return m_instantiations; }
    const std::vector<CallRecord>&   calls() const { return m_calls; }

    // ---- Scope helpers ----

    std::string currentScope() const {
        return m_scopeStack.empty() ? "" : m_scopeStack.back();
    }

    // Full "::" chain of all enclosing scopes, e.g. "MyModule::MyClass".
    std::string currentScopeChain() const {
        if (m_scopeStack.empty()) return "";
        std::string result = m_scopeStack[0];
        for (size_t i = 1; i < m_scopeStack.size(); ++i)
            result += "::" + m_scopeStack[i];
        return result;
    }

    void pushScope(const std::string& name) { m_scopeStack.push_back(name); }
    void popScope()                          { if (!m_scopeStack.empty()) m_scopeStack.pop_back(); }

    // Set endLine on the most-recently-emitted record with the given name.
    void backpatchEndLine(const std::string& name, int endLine) {
        for (int i = static_cast<int>(m_records.size()) - 1; i >= 0; --i) {
            if (m_records[i].name == name) {
                m_records[i].endLine = endLine;
                return;
            }
        }
    }

    // ---- Modules ----

    void enterModule_ansi_header(SvParser::Module_ansi_headerContext* ctx) override {
        auto* id = ctx->module_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Module, id, ctx, currentScope());
    }

    void enterModule_nonansi_header(SvParser::Module_nonansi_headerContext* ctx) override {
        auto* id = ctx->module_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Module, id, ctx, currentScope());
    }

    void exitModule_declaration(SvParser::Module_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Interfaces ----

    void enterInterface_ansi_header(SvParser::Interface_ansi_headerContext* ctx) override {
        auto* id = ctx->interface_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Interface, id, ctx, currentScope());
    }

    void enterInterface_nonansi_header(SvParser::Interface_nonansi_headerContext* ctx) override {
        auto* id = ctx->interface_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Interface, id, ctx, currentScope());
    }

    void exitInterface_declaration(SvParser::Interface_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Programs ----

    void enterProgram_ansi_header(SvParser::Program_ansi_headerContext* ctx) override {
        auto* id = ctx->program_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Program, id, ctx, currentScope());
    }

    void enterProgram_nonansi_header(SvParser::Program_nonansi_headerContext* ctx) override {
        auto* id = ctx->program_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Program, id, ctx, currentScope());
    }

    void exitProgram_declaration(SvParser::Program_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Packages ----

    void enterPackage_declaration(SvParser::Package_declarationContext* ctx) override {
        if (ctx->package_identifier().empty()) return;
        auto* id = ctx->package_identifier(0)->IDENTIFIER();
        pushId(ParseRecordKind::Package, id, ctx, currentScope());
    }

    void exitPackage_declaration(SvParser::Package_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Classes ----

    void enterClass_declaration(SvParser::Class_declarationContext* ctx) override {
        if (ctx->class_identifier().empty()) return;
        auto* id = ctx->class_identifier(0)->IDENTIFIER();
        const std::string parentClass = classTypeName(ctx->class_type());
        pushId(ParseRecordKind::Class, id, ctx, currentScope(), parentClass);
    }

    void exitClass_declaration(SvParser::Class_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Interface classes (plan.md §6.16) ----
    // Reuses ParseRecordKind::Class -- no new enum value, no dispatch
    // changes anywhere else (hover/completion/etc. already treat every
    // Class uniformly). An interface class can `extends` more than one
    // other interface class; only the first listed parent is recorded in
    // `detail`, matching the single-inheritance assumption `super.`
    // resolution already makes for ordinary classes project-wide.

    void enterInterface_class_declaration(
        SvParser::Interface_class_declarationContext* ctx) override {
        if (ctx->class_identifier().empty()) return;
        auto* id = ctx->class_identifier(0)->IDENTIFIER();
        std::string parentClass;
        if (!ctx->interface_class_type().empty())
            if (auto* pci = ctx->interface_class_type(0)->ps_class_identifier())
                if (auto* ci = pci->class_identifier())
                    if (auto* pid = ci->IDENTIFIER()) {
                        const auto pkg = packageScopeName(pci->package_scope());
                        parentClass = pkg.empty() ? pid->getText() : pkg + "::" + pid->getText();
                    }
        pushId(ParseRecordKind::Class, id, ctx, currentScope(), parentClass);
    }

    void exitInterface_class_declaration(
        SvParser::Interface_class_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Functions ----

    void enterFunction_body_declaration(
        SvParser::Function_body_declarationContext* ctx) override {
        if (ctx->function_identifier().empty()) return;
        auto* id = ctx->function_identifier(0)->IDENTIFIER();
        std::string retType;
        if (auto* fdt = ctx->function_data_type_or_implicit())
            retType = fdt->getText();
        if (auto* cs = ctx->class_scope())
            pushOutOfClassBody(ParseRecordKind::Function, id, cs, retType);
        else
            pushId(ParseRecordKind::Function, id, ctx, currentScope(), retType);
    }

    void exitFunction_body_declaration(SvParser::Function_body_declarationContext* ctx) override {
        backpatchEndLine(lastScopeSegment(currentScope()), translatedEndLine(ctx->stop));
        popScope();
    }

    // ---- Function/task prototypes -- no body (plan.md §6.16) ----
    // Covers pure virtual, extern, and interface-class methods (all reach
    // function_prototype/task_prototype via method_prototype), plus DPI
    // imports (dpi_function_proto/dpi_task_proto reduce to the same two
    // rules) -- one listener pair for all four shapes, no per-context
    // dispatch needed. pushId() unconditionally pushes a scope frame for a
    // Function/Task record on the assumption a body will eventually pop it
    // (exitFunction_body_declaration/exitTask_body_declaration); a
    // prototype has no body, so its own exit listener must pop that frame
    // immediately here instead, or every symbol declared afterward in the
    // file would be wrongly nested under it. No backpatchEndLine call is
    // needed (unlike the body-form exits): a leaf/no-body record correctly
    // keeps endLine == 0, already ParseRecord::endLine's convention for
    // Port/Signal/Parameter/Macro.

    void enterFunction_prototype(SvParser::Function_prototypeContext* ctx) override {
        auto* fid = ctx->function_identifier();
        if (!fid) return;
        std::string retType;
        if (auto* dtv = ctx->data_type_or_void())
            retType = dtv->getText();
        pushId(ParseRecordKind::Function, fid->IDENTIFIER(), ctx, currentScope(), retType);
    }

    void exitFunction_prototype(SvParser::Function_prototypeContext*) override {
        popScope();
    }

    // ---- Tasks ----

    void enterTask_body_declaration(
        SvParser::Task_body_declarationContext* ctx) override {
        if (ctx->task_identifier().empty()) return;
        auto* id = ctx->task_identifier(0)->IDENTIFIER();
        if (auto* cs = ctx->class_scope())
            pushOutOfClassBody(ParseRecordKind::Task, id, cs);
        else
            pushId(ParseRecordKind::Task, id, ctx, currentScope());
    }

    void exitTask_body_declaration(SvParser::Task_body_declarationContext* ctx) override {
        backpatchEndLine(lastScopeSegment(currentScope()), translatedEndLine(ctx->stop));
        popScope();
    }

    void enterTask_prototype(SvParser::Task_prototypeContext* ctx) override {
        auto* tid = ctx->task_identifier();
        if (!tid) return;
        pushId(ParseRecordKind::Task, tid->IDENTIFIER(), ctx, currentScope());
    }

    void exitTask_prototype(SvParser::Task_prototypeContext*) override {
        popScope();
    }

    // ---- Function/task parameters (plan.md §6.22 follow-up) ----
    // Reuses ParseRecordKind::Port -- a function/task parameter is
    // semantically the same shape a module port already is (name +
    // direction + type), so SignatureHelpProvider's existing
    // findSymbolsInScope(scope) -> filter-to-Port -> sort-by-(line,col)
    // pipeline works unchanged for these too. Fires while the enclosing
    // function/task's own scope is already on the stack (pushId already
    // pushed it in enter*_body_declaration/enter*_prototype above, which run
    // before their children), so currentScopeChain() here is
    // "...::funcName", exactly matching a module's own ports being scoped to
    // "...::moduleName".
    void enterTf_port_item(SvParser::Tf_port_itemContext* ctx) override {
        // Direction and type can be inherited from earlier arguments
        // (comma shorthand), so resolve the list up to this item.
        auto* list = dynamic_cast<SvParser::Tf_port_listContext*>(ctx->parent);
        if (!list) return;
        TfPort port;
        for (auto* item : list->tf_port_item()) {
            port = effectiveTfPort(item, port, list);
            if (item == ctx) break;
        }
        if (!port.name) return; // no name -- nothing to record
        const std::string& dir  = port.dir;
        const std::string& type = port.type;

        // Only call out a non-default direction in the label -- the
        // overwhelming majority of real parameters are plain (implicit
        // `input`), and showing it on every single one would be noise
        // ("input int width, input bit valid, ..."); `output`/`inout`/`ref`/
        // `const ref` are genuinely informative, so those are kept.
        std::string prefix = (dir == "input") ? type
                            : type.empty()     ? dir
                                                : dir + " " + type;

        // A default value is rendered *after* the name ("int width = 8"),
        // unlike direction/type which precede it -- stored past a sentinel
        // byte (never produced by real SV source) so portLabel() in
        // signature_help.cpp can split prefix/suffix back apart. Module/
        // interface/program ports never contain this sentinel, so their
        // existing "<direction> <name>" rendering is unaffected.
        std::string detail = prefix;
        if (auto* expr = ctx->expression()) {
            detail += PARAM_DEFAULT_VALUE_SEP;
            detail += " = " + m_tokens->getText(expr);
        }

        pushId(ParseRecordKind::Port, port.name, ctx, currentScope(), detail);
    }

    // ---- Ports (ANSI style) ----

    void enterAnsi_port_declaration(
        SvParser::Ansi_port_declarationContext* ctx) override {
        auto* portId = ctx->port_identifier();
        if (!portId) return;
        // Extract direction and type from whichever header is present --
        // net_port_header/variable_port_header are mutually exclusive
        // alternatives of the same rule, so at most one is ever non-null.
        // interface_port_header carries no separate type: the header's own
        // interface name already *is* the port's type.
        std::string dir;
        std::string type;
        if (auto* nh = ctx->net_port_header()) {
            if (auto* pd = nh->port_direction()) dir = pd->getText();
            if (auto* npt = nh->net_port_type()) type = m_tokens->getText(npt);
        } else if (auto* vh = ctx->variable_port_header()) {
            if (auto* pd = vh->port_direction()) dir = pd->getText();
            if (auto* vpt = vh->variable_port_type()) type = m_tokens->getText(vpt);
        }
        if (dir.empty())
            if (auto* pd = ctx->port_direction()) dir = pd->getText();

        std::string prefix = type.empty() ? dir
                            : dir.empty() ? type
                                          : dir + " " + type;

        // A default value on a module port (`input int width = 8`) was
        // previously silently dropped, the same "detail only ever carried
        // direction" gap as the missing type -- rendered the same way
        // enterTf_port_item already does (after the name, split back apart
        // by portLabel() via PARAM_DEFAULT_VALUE_SEP).
        std::string detail = prefix;
        if (auto* ce = ctx->constant_expression()) {
            detail += PARAM_DEFAULT_VALUE_SEP;
            detail += " = " + m_tokens->getText(ce);
        }

        pushId(ParseRecordKind::Port, portId->IDENTIFIER(), ctx, currentScope(), detail);
    }

    // ---- Signals: variable declarations ----

    void enterData_declaration(SvParser::Data_declarationContext* ctx) override {
        auto* list = ctx->list_of_variable_decl_assignments();
        if (!list) return;
        if (isMisparsedAssignment(ctx)) return;
        const std::string typeName = userTypeName(ctx->data_type_or_implicit());
        const std::string bareTag  = builtinBareTypeTag(ctx->data_type_or_implicit());
        // Element tag/type text to append after any container-dimension
        // tags below -- bareTag ($string/$event) wins over a genuine
        // class/interface typeName the same way it always has (a built-in
        // bare-literal data_type alternative can never also carry a
        // type_identifier()/class_type() accessor, so at most one of these
        // is ever non-empty).
        const std::string elementTag = !bareTag.empty() ? bareTag : typeName;
        for (auto* vda : list->variable_decl_assignment()) {
            auto* vi = vda->variable_identifier();
            if (!vi) continue;
            auto* id = vi->IDENTIFIER();
            if (!id) continue;
            // Layered detail (plan.md §6.15): every container-dimension tag
            // on this declarator, outermost first, followed by the element
            // type/tag if known -- e.g. "$fixed_array:$queue:MyClass" for
            // "MyClass arr[4][$]", or just "$queue" for "int q[$]" (no
            // element tag: a built-in scalar has none). A declarator with
            // no container dimensions at all falls back to elementTag alone
            // (single layer), unchanged from §6.10/§6.13's original
            // behavior.
            std::vector<std::string> layers = containerDimensionTags(vda);
            if (!elementTag.empty()) layers.push_back(elementTag);
            std::string detail;
            for (size_t i = 0; i < layers.size(); ++i) {
                if (i) detail += ':';
                detail += layers[i];
            }
            pushId(ParseRecordKind::Signal, id, vda, currentScope(), detail);
        }
    }

    // ---- Signals: net declarations ----

    void enterNet_declaration(SvParser::Net_declarationContext* ctx) override {
        // A bare `MyClass foo;` is grammatically ambiguous between
        // data_declaration (MyClass classified as a data_type's
        // type_identifier) and this rule's own alt 2 (MyClass classified as
        // a net_type_identifier — a user-defined nettype); this grammar
        // resolves that case here, not in enterData_declaration. Only alt 2
        // sets net_type_identifier(); alt 1's ordinary net_type ... data_type
        // form still goes through the normal userTypeName() path.
        const std::string typeName = ctx->net_type_identifier()
            ? ctx->net_type_identifier()->IDENTIFIER()->getText()
            : userTypeName(ctx->data_type_or_implicit());

        // Form 1: comma-separated assignment list
        if (auto* list = ctx->list_of_net_decl_assignments()) {
            for (auto* nda : list->net_decl_assignment()) {
                auto* ni = nda->net_identifier();
                if (!ni || !ni->IDENTIFIER()) continue;
                pushId(ParseRecordKind::Signal, ni->IDENTIFIER(), nda, currentScope(), typeName);
            }
        }
        // Form 2: bare net_identifier list (alternative grammar production)
        for (auto* ni : ctx->net_identifier()) {
            if (ni->IDENTIFIER())
                pushId(ParseRecordKind::Signal, ni->IDENTIFIER(), ctx, currentScope(), typeName);
        }
    }

    // ---- Typedefs, enum literals, struct/union members, genvars (§6.30 step C) ----

    void enterType_declaration(SvParser::Type_declarationContext* ctx) override {
        const auto ids = ctx->type_identifier();
        if (ctx->data_type()) { // typedef <data_type> name;
            if (!ids.empty())
                pushId(ParseRecordKind::Typedef, ids[0]->IDENTIFIER(), ctx, currentScope(),
                       dataTypeName(ctx->data_type()));
        } else if (ctx->interface_instance_identifier()) { // typedef intf.T name;
            if (ids.size() >= 2)
                pushId(ParseRecordKind::Typedef, ids[1]->IDENTIFIER(), ctx, currentScope(),
                       ids[0]->getText());
        }
        // A forward `typedef [class|enum|...] name;` is deliberately not
        // recorded: it would compete with the real declaration.
    }

    void enterEnum_name_declaration(SvParser::Enum_name_declarationContext* ctx) override {
        auto* ei = ctx->enum_identifier();
        if (!ei) return;
        // detail: the enum's typedef name, if it has one.
        std::string typeName;
        if (auto* dt = dynamic_cast<SvParser::Data_typeContext*>(ctx->parent))
            if (auto* td = dynamic_cast<SvParser::Type_declarationContext*>(dt->parent))
                if (!td->type_identifier().empty())
                    typeName = td->type_identifier(0)->getText();
        pushId(ParseRecordKind::EnumLiteral, ei->IDENTIFIER(), ctx, currentScope(), typeName);
    }

    void enterStruct_union_member(SvParser::Struct_union_memberContext* ctx) override {
        auto owner = structOwnerPath(dynamic_cast<SvParser::Data_typeContext*>(ctx->parent));
        if (!owner) return;
        const std::string chain = currentScopeChain();
        const std::string scope = chain.empty() ? *owner : chain + "::" + *owner;
        const std::string typeName =
            ctx->data_type_or_void() ? dataTypeName(ctx->data_type_or_void()->data_type()) : "";
        auto* list = ctx->list_of_variable_decl_assignments();
        if (!list) return;
        for (auto* vda : list->variable_decl_assignment())
            if (auto* vi = vda->variable_identifier())
                pushIdInScope(ParseRecordKind::Member, vi->IDENTIFIER(), scope, currentScope(),
                              typeName);
    }

    void enterGenvar_declaration(SvParser::Genvar_declarationContext* ctx) override {
        if (auto* list = ctx->list_of_genvar_identifiers())
            for (auto* gi : list->genvar_identifier())
                pushId(ParseRecordKind::Genvar, gi->IDENTIFIER(), ctx, currentScope());
    }

    void enterGenvar_initialization(SvParser::Genvar_initializationContext* ctx) override {
        // Only `for (genvar g = ...)` declares; `for (g = ...)` uses a genvar.
        if (ctx->children.empty() || ctx->children[0]->getText() != "genvar") return;
        if (auto* gi = ctx->genvar_identifier())
            pushId(ParseRecordKind::Genvar, gi->IDENTIFIER(), ctx, currentScope());
    }

    // ---- Package imports / exports ----

    void enterPackage_export_declaration(
        SvParser::Package_export_declarationContext* /*ctx*/) override {
        m_inExport = true;
    }

    void exitPackage_export_declaration(
        SvParser::Package_export_declarationContext* /*ctx*/) override {
        m_inExport = false;
    }

    void enterPackage_import_item(SvParser::Package_import_itemContext* ctx) override {
        auto* pkgCtx = ctx->package_identifier();
        if (!pkgCtx) return;
        auto* pkgId = pkgCtx->IDENTIFIER();
        if (!pkgId) return;
        std::string pkg  = pkgId->getText();
        std::string item = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "*";
        auto* tok = pkgId->getSymbol();
        auto [file, line] = translateLine(static_cast<int>(tok->getLine()), m_sourceMap);
        m_imports.push_back({pkg, item, line, file, m_inExport});
    }

    // ---- Instantiations (module/interface/program references) ----

    void enterModule_instantiation(SvParser::Module_instantiationContext* ctx) override {
        recordInstantiations(ctx->module_identifier()->IDENTIFIER(), ctx->hierarchical_instance());
    }

    void enterInterface_instantiation(SvParser::Interface_instantiationContext* ctx) override {
        recordInstantiations(ctx->interface_identifier()->IDENTIFIER(), ctx->hierarchical_instance());
    }

    void enterProgram_instantiation(SvParser::Program_instantiationContext* ctx) override {
        recordInstantiations(ctx->program_identifier()->IDENTIFIER(), ctx->hierarchical_instance());
    }

    // ---- Function/task calls (plan.md §6.23) ----
    // tf_call: ps_or_hierarchical_tf_identifier attribute_instance*
    //          ('(' list_of_arguments ')')?
    // -- reaches this same rule for both bare (`my_func(`) and dotted
    // (`obj.method(`) calls, since ps_or_hierarchical_tf_identifier's own
    // hierarchical_tf_identifier alternative degenerates to a bare
    // identifier with zero '.'-separated prefix segments; distinguishing
    // them is a property of the extracted text, not the grammar shape.
    // Scoped to bare calls only, matching §6.22 follow-up's own signature-
    // help scope limit -- a dotted call needs completion's own chain-
    // resolution machinery, not this check.
    void enterTf_call(SvParser::Tf_callContext* ctx) override {
        auto* idCtx = ctx->ps_or_hierarchical_tf_identifier();
        if (!idCtx) return;

        // A real hierarchical/dotted reference always has a literal '.' in
        // its own text; a bare name never does -- package-scoped
        // ("pkg::foo(") and class-scoped ("Class::foo(") forms use "::",
        // not ".", so they fall through to the bare-name handling below
        // unchanged (matching signature_help.cpp's own equally permissive
        // treatment of a package/class-qualified call).
        const std::string idText = m_tokens->getText(idCtx);
        if (idText.find('.') != std::string::npos) return;

        // The callee's own bare name is always the last token of this
        // subtree regardless of alternative matched -- a "pkg::"/"Class::"
        // prefix, if present, always precedes it, never splits it.
        std::string name = idCtx->getStop()->getText();
        std::string scope = extractCalleeScope(idText);

        auto* tok = idCtx->getStart();
        int compiledLine = static_cast<int>(tok->getLine());
        int compiledCol  = static_cast<int>(tok->getCharPositionInLine());
        auto [file, line] = translateLine(compiledLine, m_sourceMap);
        int col = translateColumn(compiledLine, compiledCol, m_sourceMap);

        m_calls.push_back({std::move(name), std::move(scope),
                            extractArgSlots(ctx->list_of_arguments()), line, col, file});
    }

    // ---- Parameters ----

    void enterParameter_declaration(SvParser::Parameter_declarationContext* ctx) override {
        extractParams(ctx->list_of_param_assignments(), userTypeName(ctx->data_type_or_implicit()));
    }

    void enterLocal_parameter_declaration(
        SvParser::Local_parameter_declarationContext* ctx) override {
        extractParams(ctx->list_of_param_assignments(), userTypeName(ctx->data_type_or_implicit()));
    }

private:
    const std::vector<SourceLine>& m_sourceMap;
    antlr4::CommonTokenStream* m_tokens;
    std::vector<ParseRecord>  m_records;
    std::vector<ImportRecord> m_imports;
    std::vector<InstantiationRecord> m_instantiations;
    std::vector<CallRecord>   m_calls;
    std::vector<std::string>  m_scopeStack;
    bool                      m_inExport{false};

    // One tf_port_item's name, direction and type after LRM 13.3's
    // inheritance rules. A default-constructed TfPort stands for "no
    // previous argument".
    struct TfPort {
        antlr4::tree::TerminalNode* name{nullptr};
        std::string dir{"input"};
        std::string type;
        bool isFirst{true};
    };

    // `item` resolved against the argument before it: a missing direction is
    // inherited; a missing type is `logic` on the first argument or after an
    // explicit direction, else inherited. A bare-identifier "type" with no
    // port_identifier is the argument's name -- always in a body (names are
    // mandatory there), and in a prototype only when the previous argument
    // was named (`f(int, my_t)` keeps `my_t` as an unnamed argument's type).
    TfPort effectiveTfPort(SvParser::Tf_port_itemContext* item, const TfPort& prev,
                           SvParser::Tf_port_listContext* list) const {
        TfPort p;
        p.isFirst = false;
        auto* dtoi = item->data_type_or_implicit();
        std::string typeText = dtoi ? m_tokens->getText(dtoi) : "";
        if (auto* pid = item->port_identifier()) {
            p.name = pid->IDENTIFIER();
        } else if (auto* bare = bareTypeIdentifier(dtoi)) {
            const bool prototype =
                dynamic_cast<SvParser::Function_prototypeContext*>(list->parent) ||
                dynamic_cast<SvParser::Task_prototypeContext*>(list->parent) ||
                dynamic_cast<SvParser::Class_constructor_prototypeContext*>(list->parent);
            if (!prototype || prev.name) {
                p.name = bare;
                typeText.clear();
            }
        }
        auto* tpd = item->tf_port_direction();
        p.dir = tpd ? m_tokens->getText(tpd) : prev.dir;
        if (!typeText.empty())            p.type = typeText;
        else if (prev.isFirst || tpd)     p.type = "logic";
        else                              p.type = prev.type;
        return p;
    }

    // Translate a stop token's line through the source map; returns 0 if token is null.
    int translatedEndLine(antlr4::Token* stop) const {
        if (!stop) return 0;
        auto [f, line] = translateLine(static_cast<int>(stop->getLine()), m_sourceMap);
        return line;
    }

    // Push using the identifier token's position (more precise than the rule start).
    void pushId(ParseRecordKind kind, antlr4::tree::TerminalNode* id,
                antlr4::ParserRuleContext* /*ctx*/,
                const std::string& parent = "", const std::string& detail = "") {
        if (!id) return;
        pushIdInScope(kind, id, currentScopeChain(), parent, detail);
        // Push this record's name onto the scope stack so nested declarations
        // have it as their parent. Only top-level named scopes push here.
        if (kind == ParseRecordKind::Module   ||
            kind == ParseRecordKind::Interface ||
            kind == ParseRecordKind::Package   ||
            kind == ParseRecordKind::Class     ||
            kind == ParseRecordKind::Function  ||
            kind == ParseRecordKind::Task      ||
            kind == ParseRecordKind::Program) {
            pushScope(id->getText());
        }
    }

    // An out-of-class method body, `function/task C::m(...)` (plan.md §6.30
    // step D): the row is recorded in the class's own scope (next to the
    // extern prototype) and the body's scope frame is "C::m", so its locals
    // and arguments nest under `<class scope>::m` and the class's members are
    // on their lexical scope chain. The written qualifier is taken relative to
    // the current scope chain (`Outer::Inner` inside package p is
    // p::Outer::Inner); leading segments that restate the enclosing frames
    // are dropped (`p::C` written inside package p is p::C).
    void pushOutOfClassBody(ParseRecordKind kind, antlr4::tree::TerminalNode* id,
                            SvParser::Class_scopeContext* cs, const std::string& detail = "") {
        if (!id) return;
        std::vector<std::string> segs;
        const std::string written = classTypeName(cs->class_type());
        for (size_t start = 0;;) {
            size_t sep = written.find("::", start);
            segs.push_back(written.substr(start, sep == std::string::npos ? std::string::npos
                                                                          : sep - start));
            if (sep == std::string::npos) break;
            start = sep + 2;
        }
        if (segs.empty() || segs.back().empty()) {
            pushId(kind, id, nullptr, currentScope(), detail);
            return;
        }
        // Drop leading qualifier segments that restate the enclosing frames.
        size_t skip = 0;
        while (skip < m_scopeStack.size() && skip + 1 < segs.size() &&
               segs[skip] == m_scopeStack[skip])
            ++skip;
        std::string rel;
        for (size_t i = skip; i < segs.size(); ++i) rel += (rel.empty() ? "" : "::") + segs[i];

        const std::string chain      = currentScopeChain();
        const std::string classScope = chain.empty() ? rel : chain + "::" + rel;
        pushIdInScope(kind, id, classScope, segs.back(), detail);
        pushScope(rel + "::" + id->getText());
    }

    static std::string lastScopeSegment(const std::string& frame) {
        auto sep = frame.rfind("::");
        return sep == std::string::npos ? frame : frame.substr(sep + 2);
    }

    // Records `id` with an explicit `scope` instead of the scope stack's
    // chain (struct members live in a scope named after their owner, which
    // is never on the stack). Never pushes a scope.
    void pushIdInScope(ParseRecordKind kind, antlr4::tree::TerminalNode* id,
                       const std::string& scope, const std::string& parent = "",
                       const std::string& detail = "") {
        if (!id) return;
        auto* tok = id->getSymbol();
        int compiledLine = static_cast<int>(tok->getLine());
        int compiledCol  = static_cast<int>(tok->getCharPositionInLine());
        auto [file, line] = translateLine(compiledLine, m_sourceMap);
        int col = translateColumn(compiledLine, compiledCol, m_sourceMap);
        m_records.push_back({kind, id->getText(), line, col, parent, detail, 0, scope, file});
    }

    void extractParams(SvParser::List_of_param_assignmentsContext* list,
                        const std::string& typeName = "") {
        if (!list) return;
        for (auto* pa : list->param_assignment()) {
            auto* pi = pa->parameter_identifier();
            if (!pi || !pi->IDENTIFIER()) continue;
            pushId(ParseRecordKind::Parameter, pi->IDENTIFIER(), pa, currentScope(), typeName);
        }
    }

    // Emits one InstantiationRecord per hierarchical_instance sharing `typeId`
    // (covers comma-separated instances: `Foo u0(...), u1(...);`).
    void recordInstantiations(
        antlr4::tree::TerminalNode* typeId,
        const std::vector<SvParser::Hierarchical_instanceContext*>& instances) {
        if (!typeId) return;
        std::string type = typeId->getText();
        int fallbackLine = static_cast<int>(typeId->getSymbol()->getLine());
        for (auto* hi : instances) {
            std::string instName;
            int line = fallbackLine;
            if (auto* noi = hi->name_of_instance()) {
                if (auto* ii = noi->instance_identifier()) {
                    if (auto* id = ii->IDENTIFIER()) {
                        instName = id->getText();
                        line = static_cast<int>(id->getSymbol()->getLine());
                    }
                }
            }
            auto [file, origLine] = translateLine(line, m_sourceMap);
            m_instantiations.push_back({type, instName, origLine, file});
        }
    }
};

// ---------------------------------------------------------------------------
// SvTreeWalker::walk
// ---------------------------------------------------------------------------

WalkResult SvTreeWalker::walk(const std::string& source,
                               const std::vector<SourceLine>& sourceMap) {
    antlr4::ANTLRInputStream input(source);
    SvLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SvParser parser(&tokens);

    SvErrorListener errListener(sourceMap);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errListener);
    parser.removeErrorListeners();
    parser.addErrorListener(&errListener);

    antlr4::tree::ParseTree* tree = parser.source_text();

    SvRecordListener listener(sourceMap, &tokens);
    antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

    return {listener.records(), errListener.errors(), listener.imports(), listener.instantiations(),
            listener.calls()};
}
