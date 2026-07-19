#pragma once
#include <string>
#include <vector>

// One column-drift breakpoint introduced by a mid-line macro expansion.
// For output columns >= outputCol on the owning line, add `delta` to get the
// corresponding column in the original (unexpanded) source line.
struct ColShift {
    int outputCol;  // 0-based column in the expanded output line
    int delta;      // original column = outputCol + delta, for outputCol >= this
};

// Maps one preprocessor output line to its original source location.
struct SourceLine {
    std::string file;  // original file path; empty = same as compiled file
    int         line;  // 1-based line in `file`
    std::vector<ColShift> colShifts{};  // column breakpoints from mid-line macro expansion
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
    Program,
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

// One package import/export statement: `import pkgName::item`, `import pkgName::*`,
// `export pkgName::item`, or `export pkgName::*`.
struct ImportRecord {
    std::string pkgName;   // package being imported/exported
    std::string item;      // symbol name, or "*" for wildcard
    int         line{0};   // 1-based line in `file`
    std::string file{};    // empty = same as compiled file
    bool        isExport{false}; // true for `export`, false for plain `import`
};

// One module/interface/program instantiation: `Foo u0 (...);` — a reference
// to a design unit that may or may not be declared anywhere yet known.
struct InstantiationRecord {
    std::string typeName;   // module/interface/program identifier being instantiated
    std::string instName;   // instance_identifier text (name_of_instance)
    int         line{0};    // 1-based line in `file`
    std::string file{};     // empty = same as compiled file
};
