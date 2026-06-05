#include "compiler/sv_tree_walker.h"
#include "SvBaseListener.h"
#include "SvLexer.h"
#include "SvParser.h"
#include <antlr4-runtime.h>

// ---------------------------------------------------------------------------
// SvRecordListener — internal ANTLR4 listener that fills ParseRecord[]
// ---------------------------------------------------------------------------

class SvRecordListener : public SvBaseListener {
public:
    const std::vector<ParseRecord>& records() const { return m_records; }

    // ---- Scope helpers ----

    std::string currentScope() const {
        return m_scopeStack.empty() ? "" : m_scopeStack.back();
    }

    void pushScope(const std::string& name) { m_scopeStack.push_back(name); }
    void popScope()                          { if (!m_scopeStack.empty()) m_scopeStack.pop_back(); }

    // ---- Modules ----

    void enterModule_ansi_header(SvParser::Module_ansi_headerContext* ctx) override {
        auto* id = ctx->module_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Module, id, ctx, currentScope());
    }

    void enterModule_nonansi_header(SvParser::Module_nonansi_headerContext* ctx) override {
        auto* id = ctx->module_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Module, id, ctx, currentScope());
    }

    void exitModule_declaration(SvParser::Module_declarationContext*) override { popScope(); }

    // ---- Interfaces ----

    void enterInterface_ansi_header(SvParser::Interface_ansi_headerContext* ctx) override {
        auto* id = ctx->interface_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Interface, id, ctx, currentScope());
    }

    void enterInterface_nonansi_header(SvParser::Interface_nonansi_headerContext* ctx) override {
        auto* id = ctx->interface_identifier()->IDENTIFIER();
        pushId(ParseRecordKind::Interface, id, ctx, currentScope());
    }

    void exitInterface_declaration(SvParser::Interface_declarationContext*) override { popScope(); }

    // ---- Packages ----

    void enterPackage_declaration(SvParser::Package_declarationContext* ctx) override {
        if (ctx->package_identifier().empty()) return;
        auto* id = ctx->package_identifier(0)->IDENTIFIER();
        pushId(ParseRecordKind::Package, id, ctx, currentScope());
    }

    void exitPackage_declaration(SvParser::Package_declarationContext*) override { popScope(); }

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

    void exitClass_declaration(SvParser::Class_declarationContext*) override { popScope(); }

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

    void exitFunction_body_declaration(SvParser::Function_body_declarationContext*) override {
        popScope();
    }

    // ---- Tasks ----

    void enterTask_body_declaration(
        SvParser::Task_body_declarationContext* ctx) override {
        if (ctx->task_identifier().empty()) return;
        auto* id = ctx->task_identifier(0)->IDENTIFIER();
        pushId(ParseRecordKind::Task, id, ctx, currentScope());
    }

    void exitTask_body_declaration(SvParser::Task_body_declarationContext*) override {
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

    // ---- Parameters ----

    void enterParameter_declaration(SvParser::Parameter_declarationContext* ctx) override {
        extractParams(ctx->list_of_param_assignments());
    }

    void enterLocal_parameter_declaration(
        SvParser::Local_parameter_declarationContext* ctx) override {
        extractParams(ctx->list_of_param_assignments());
    }

private:
    std::vector<ParseRecord> m_records;
    std::vector<std::string> m_scopeStack;

    // Push using the identifier token's position (more precise than the rule start).
    void pushId(ParseRecordKind kind, antlr4::tree::TerminalNode* id,
                antlr4::ParserRuleContext* /*ctx*/,
                const std::string& parent = "", const std::string& detail = "") {
        if (!id) return;
        auto* tok = id->getSymbol();
        m_records.push_back({kind, id->getText(),
                              static_cast<int>(tok->getLine()),
                              static_cast<int>(tok->getCharPositionInLine()),
                              parent, detail});
        // Push this record's name onto the scope stack so nested declarations
        // have it as their parent. Only top-level named scopes push here.
        if (kind == ParseRecordKind::Module   ||
            kind == ParseRecordKind::Interface ||
            kind == ParseRecordKind::Package   ||
            kind == ParseRecordKind::Class     ||
            kind == ParseRecordKind::Function  ||
            kind == ParseRecordKind::Task) {
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
};

// ---------------------------------------------------------------------------
// SvTreeWalker::walk
// ---------------------------------------------------------------------------

WalkResult SvTreeWalker::walk(const std::string& source) {
    antlr4::ANTLRInputStream input(source);
    SvLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SvParser parser(&tokens);

    lexer.removeErrorListeners();
    parser.removeErrorListeners();

    antlr4::tree::ParseTree* tree = parser.source_text();
    int parseErrors = static_cast<int>(parser.getNumberOfSyntaxErrors());

    SvRecordListener listener;
    antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

    return {listener.records(), parseErrors};
}
