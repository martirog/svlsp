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

    // ---- Modules ----

    void enterModule_ansi_header(SvParser::Module_ansi_headerContext* ctx) override {
        push(ParseRecordKind::Module, ctx->module_identifier()->IDENTIFIER(), ctx);
    }

    void enterModule_nonansi_header(SvParser::Module_nonansi_headerContext* ctx) override {
        push(ParseRecordKind::Module, ctx->module_identifier()->IDENTIFIER(), ctx);
    }

    // ---- Interfaces ----

    void enterInterface_ansi_header(SvParser::Interface_ansi_headerContext* ctx) override {
        push(ParseRecordKind::Interface, ctx->interface_identifier()->IDENTIFIER(), ctx);
    }

    void enterInterface_nonansi_header(SvParser::Interface_nonansi_headerContext* ctx) override {
        push(ParseRecordKind::Interface, ctx->interface_identifier()->IDENTIFIER(), ctx);
    }

    // ---- Packages ----

    void enterPackage_declaration(SvParser::Package_declarationContext* ctx) override {
        // package_identifier(0) is the opening name; (1) is the optional endpackage label
        if (!ctx->package_identifier().empty())
            push(ParseRecordKind::Package, ctx->package_identifier(0)->IDENTIFIER(), ctx);
    }

    // ---- Classes ----

    void enterClass_declaration(SvParser::Class_declarationContext* ctx) override {
        // class_identifier(0) is the class name; subsequent ones are endclass labels
        if (!ctx->class_identifier().empty())
            push(ParseRecordKind::Class, ctx->class_identifier(0)->IDENTIFIER(), ctx);
    }

    // ---- Functions ----

    void enterFunction_body_declaration(
        SvParser::Function_body_declarationContext* ctx) override {
        // function_identifier(0) is the function name; (1) is the endfunction label
        if (!ctx->function_identifier().empty())
            push(ParseRecordKind::Function, ctx->function_identifier(0)->IDENTIFIER(), ctx);
    }

    // ---- Tasks ----

    void enterTask_body_declaration(
        SvParser::Task_body_declarationContext* ctx) override {
        // task_identifier(0) is the task name; (1) is the endtask label
        if (!ctx->task_identifier().empty())
            push(ParseRecordKind::Task, ctx->task_identifier(0)->IDENTIFIER(), ctx);
    }

    // ---- Ports (ANSI style) ----

    void enterAnsi_port_declaration(
        SvParser::Ansi_port_declarationContext* ctx) override {
        auto* portId = ctx->port_identifier();
        if (portId)
            push(ParseRecordKind::Port, portId->IDENTIFIER(), ctx);
    }

private:
    std::vector<ParseRecord> m_records;

    void push(ParseRecordKind kind, antlr4::tree::TerminalNode* id,
              antlr4::ParserRuleContext* ctx) {
        if (!id) return;
        m_records.push_back({kind, id->getText(),
                              static_cast<int>(ctx->getStart()->getLine()),
                              static_cast<int>(ctx->getStart()->getCharPositionInLine())});
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
