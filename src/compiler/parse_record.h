#pragma once
#include <string>

// Maps one preprocessor output line to its original source location.
struct SourceLine {
    std::string file;  // original file path; empty = same as compiled file
    int         line;  // 1-based line in `file`
};

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
    int             line;          // 1-based in `file`
    int             column;        // 0-based
    std::string     parent;        // immediate enclosing scope name (e.g. "MyClass")
    std::string     detail;        // kind-specific: port direction, class parent, return type, macro body
    int             endLine{0};    // 1-based; last line of scope body; 0 for leaf symbols
    std::string     scope{};       // full enclosing scope chain, e.g. "MyModule::MyClass"
    std::string     file{};        // original source file; empty = same as compiled file
};

struct ParseError {
    int         line;    // 1-based (ANTLR4 convention)
    int         column;  // 0-based
    std::string message;
    std::string file{};  // original source file; empty = same as compiled file
};

// One package import statement: `import pkgName::item` or `import pkgName::*`.
struct ImportRecord {
    std::string pkgName;   // package being imported
    std::string item;      // symbol name, or "*" for wildcard
    int         line{0};   // 1-based line in `file`
    std::string file{};    // empty = same as compiled file
};
