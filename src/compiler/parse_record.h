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
    int             line;   // 1-based
    int             column; // 0-based
    std::string     parent; // containing scope name (empty if top-level)
    std::string     detail; // kind-specific: port direction, class parent, return type, macro body
};

struct ParseError {
    int         line;    // 1-based (ANTLR4 convention)
    int         column;  // 0-based
    std::string message;
};
