#include "compiler/sv_tree_walker.h"
#include "SvBaseListener.h"
#include "SvLexer.h"
#include "SvParser.h"
#include <antlr4-runtime.h>

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
static std::string userTypeName(SvParser::Data_type_or_implicitContext* dtoi)
{
    if (!dtoi) return "";
    auto* dt = dtoi->data_type();
    if (!dt) return "";
    if (auto* ti = dt->type_identifier())
        return ti->IDENTIFIER() ? ti->IDENTIFIER()->getText() : "";
    if (auto* ct = dt->class_type())
        if (auto* psci = ct->ps_class_identifier())
            if (auto* ci = psci->class_identifier())
                return ci->IDENTIFIER() ? ci->IDENTIFIER()->getText() : "";
    return "";
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
        std::string parentClass;
        if (auto* ct = ctx->class_type())
            if (auto* pci = ct->ps_class_identifier())
                if (auto* ci = pci->class_identifier())
                    if (auto* pid = ci->IDENTIFIER())
                        parentClass = pid->getText();
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
                    if (auto* pid = ci->IDENTIFIER())
                        parentClass = pid->getText();
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
        pushId(ParseRecordKind::Function, id, ctx, currentScope(), retType);
    }

    void exitFunction_body_declaration(SvParser::Function_body_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
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
        pushId(ParseRecordKind::Task, id, ctx, currentScope());
    }

    void exitTask_body_declaration(SvParser::Task_body_declarationContext* ctx) override {
        backpatchEndLine(currentScope(), translatedEndLine(ctx->stop));
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
        auto* portId = ctx->port_identifier();
        if (!portId || !portId->IDENTIFIER()) return; // no name -- nothing to record

        // LRM: direction defaults to `input` when tf_port_direction is
        // absent; applied explicitly here rather than leaving it blank.
        std::string dir = "input";
        if (auto* tpd = ctx->tf_port_direction())
            dir = m_tokens->getText(tpd);

        std::string type;
        if (auto* dtoi = ctx->data_type_or_implicit())
            type = m_tokens->getText(dtoi);

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

        pushId(ParseRecordKind::Port, portId->IDENTIFIER(), ctx, currentScope(), detail);
    }

    // ---- Ports (ANSI style) ----

    void enterAnsi_port_declaration(
        SvParser::Ansi_port_declarationContext* ctx) override {
        auto* portId = ctx->port_identifier();
        if (!portId) return;
        // Extract direction from whichever header is present
        std::string dir;
        if (auto* nh = ctx->net_port_header())
            if (auto* pd = nh->port_direction()) dir = pd->getText();
        if (dir.empty())
            if (auto* vh = ctx->variable_port_header())
                if (auto* pd = vh->port_direction()) dir = pd->getText();
        if (dir.empty())
            if (auto* pd = ctx->port_direction()) dir = pd->getText();
        pushId(ParseRecordKind::Port, portId->IDENTIFIER(), ctx, currentScope(), dir);
    }

    // ---- Signals: variable declarations ----

    void enterData_declaration(SvParser::Data_declarationContext* ctx) override {
        auto* list = ctx->list_of_variable_decl_assignments();
        if (!list) return;
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
    std::vector<std::string>  m_scopeStack;
    bool                      m_inExport{false};

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
        auto* tok = id->getSymbol();
        int compiledLine = static_cast<int>(tok->getLine());
        int compiledCol  = static_cast<int>(tok->getCharPositionInLine());
        auto [file, line] = translateLine(compiledLine, m_sourceMap);
        int col = translateColumn(compiledLine, compiledCol, m_sourceMap);
        m_records.push_back({kind, id->getText(), line, col,
                              parent, detail, 0, currentScopeChain(), file});
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

    return {listener.records(), errListener.errors(), listener.imports(), listener.instantiations()};
}
