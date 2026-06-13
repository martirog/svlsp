#pragma once
#include <string>

enum class ParseRecordKind {
    Module,
    Interface,
    Package,
    Class,
    Function,
    Task,
    Port,
    Signal,    // variable or net declaration (logic, wire, reg, …)
    Parameter, // parameter or localparam declaration
    Macro,     // `define macro (populated by SvPreprocessor, not the grammar walker)
};

struct ParseRecord {
    ParseRecordKind kind;
    std::string     name;
    int             line;          // 1-based
    int             column;        // 0-based
    std::string     parent;        // immediate enclosing scope name (e.g. "MyClass")
    std::string     detail;        // kind-specific: port direction, class parent, return type, macro body
    int             endLine{0};    // 1-based; last line of scope body; 0 for leaf symbols
    std::string     scope{};       // full enclosing scope chain, e.g. "MyModule::MyClass"
};

struct ParseError {
    int         line;    // 1-based (ANTLR4 convention)
    int         column;  // 0-based
    std::string message;
};
