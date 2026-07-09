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
        m_errors.push_back({origLine, static_cast<int>(charPositionInLine), msg, file});
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
    explicit SvRecordListener(const std::vector<SourceLine>& sourceMap)
        : m_sourceMap(sourceMap) {}

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
        for (auto* vda : list->variable_decl_assignment()) {
            auto* vi = vda->variable_identifier();
            if (!vi) continue;
            auto* id = vi->IDENTIFIER();
            if (!id) continue;
            pushId(ParseRecordKind::Signal, id, vda, currentScope());
        }
    }

    // ---- Signals: net declarations ----

    void enterNet_declaration(SvParser::Net_declarationContext* ctx) override {
        // Form 1: comma-separated assignment list
        if (auto* list = ctx->list_of_net_decl_assignments()) {
            for (auto* nda : list->net_decl_assignment()) {
                auto* ni = nda->net_identifier();
                if (!ni || !ni->IDENTIFIER()) continue;
                pushId(ParseRecordKind::Signal, ni->IDENTIFIER(), nda, currentScope());
            }
        }
        // Form 2: bare net_identifier list (alternative grammar production)
        for (auto* ni : ctx->net_identifier()) {
            if (ni->IDENTIFIER())
                pushId(ParseRecordKind::Signal, ni->IDENTIFIER(), ctx, currentScope());
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
        extractParams(ctx->list_of_param_assignments());
    }

    void enterLocal_parameter_declaration(
        SvParser::Local_parameter_declarationContext* ctx) override {
        extractParams(ctx->list_of_param_assignments());
    }

private:
    const std::vector<SourceLine>& m_sourceMap;
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
        auto [file, line] = translateLine(static_cast<int>(tok->getLine()), m_sourceMap);
        m_records.push_back({kind, id->getText(), line,
                              static_cast<int>(tok->getCharPositionInLine()),
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

    void extractParams(SvParser::List_of_param_assignmentsContext* list) {
        if (!list) return;
        for (auto* pa : list->param_assignment()) {
            auto* pi = pa->parameter_identifier();
            if (!pi || !pi->IDENTIFIER()) continue;
            pushId(ParseRecordKind::Parameter, pi->IDENTIFIER(), pa, currentScope());
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

    SvRecordListener listener(sourceMap);
    antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

    return {listener.records(), errListener.errors(), listener.imports(), listener.instantiations()};
}
