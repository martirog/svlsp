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
    Typedef,     // `typedef <type> name;` (not a forward `typedef class Foo;`)
    EnumLiteral, // an enum's named value, scoped like the enum declaration
    Member,      // struct/union member, scoped `<enclosing>::<variable-or-typedef>`
    Genvar,      // `genvar g;` or an inline `for (genvar g = ...)`
    Block,       // a begin/end, fork/join or generate block that is a scope, or a
                 // for/foreach loop's own variables' scope: named by its label,
                 // else "@<line>.<col>" of its begin/fork/for/foreach keyword
};

// Reserved ParseRecord::detail values for built-in container/data types with
// implicit methods but no ParseRecordKind/DB scope of their own (queues,
// associative arrays, dynamic/fixed-size unpacked arrays, string, event).
// SV identifiers can never start with '$' (reserved for system tasks), so
// these can never collide with a real userTypeName()-extracted type name.
// Powers built-in method completion (plan.md §6.13) -- see
// src/lsp/sv_builtin_methods.h for the method tables each tag selects.
// mailbox/process/semaphore need no tag: they're real class_type references
// (userTypeName() already extracts them verbatim), matched there by literal
// name instead.
inline constexpr const char* CONTAINER_QUEUE         = "$queue";
inline constexpr const char* CONTAINER_ASSOC         = "$assoc_array";
inline constexpr const char* CONTAINER_DYNAMIC_ARRAY = "$dynamic_array";
inline constexpr const char* CONTAINER_FIXED_ARRAY   = "$fixed_array";
inline constexpr const char* CONTAINER_STRING        = "$string";
inline constexpr const char* CONTAINER_EVENT         = "$event";

// Separator byte inside a function/task parameter's Port::detail (plan.md
// §6.22 follow-up), splitting the "<direction> <type>" prefix (rendered
// before the parameter name) from the suffix rendered after it: the
// argument's unpacked dimensions as written, then an optional
// " = <default value>" -- e.g. "int\x1F = 8" for "int width = 8",
// "ref C\x1F[$]" for "ref C q[$]". A real SV source character sequence can
// never contain this byte, so a plain module/interface/program port's
// detail (just "input"/"output"/...) is never mistaken for having a suffix.
// Read it with portHasDefault() below, and portLabel()
// (src/lsp/signature_help.cpp) / portValueType() (src/lsp/symbol_utils.h);
// enterTf_port_item in src/compiler/sv_tree_walker.cpp writes it.
inline constexpr char PARAM_DEFAULT_VALUE_SEP = '\x1F';

// True if a Port's detail records a default value (see above).
inline bool portHasDefault(const std::string& detail)
{
    const auto sep = detail.find(PARAM_DEFAULT_VALUE_SEP);
    return sep != std::string::npos && detail.find(" = ", sep) != std::string::npos;
}

// A forward `typedef [class|interface class|enum|struct|union] T;` is
// recorded as a Typedef whose detail is that keyword (`type` for a bare
// `typedef T;`). A real typedef's detail is a type name or "", never a
// keyword, so the detail alone tells the two apart. The forward row is a
// fallback: the real declaration wins wherever it is visible (plan.md
// §6.30, "Follow-up: forward typedefs and declare-before-use").
inline bool isForwardTypedefDetail(const std::string& detail)
{
    return detail == "class" || detail == "interface class" || detail == "enum" ||
           detail == "struct" || detail == "union" || detail == "type";
}

struct ParseRecord {
    ParseRecordKind kind;
    std::string     name;
    int             line;          // 1-based in `file`
    int             column;        // 0-based
    std::string     parent;        // immediate enclosing scope name (e.g. "MyClass")
    std::string     detail;        // kind-specific: port direction, class parent, return type,
                                    // macro body, declared user type (Signal/Parameter/Member),
                                    // aliased type (Typedef; the keyword
                                    // of a forward typedef), enum typedef name (EnumLiteral)
    int             endLine{0};    // 1-based; last line of scope body; 0 for leaf symbols
    std::string     scope{};       // full enclosing scope chain, e.g. "MyModule::MyClass"
    std::string     file{};        // original source file; empty = same as compiled file
    std::string     doc{};         // cleaned doc comment (plan.md §6.31); "" = none
    std::string     access{};      // class member's "local" / "protected"; "" = public
    int             endCol{0};     // 0-based column just past a scope's last token
                                    // (on endLine); 0 = unknown (whole end line)
};

struct ParseError {
    int         line;    // 1-based (ANTLR4 convention)
    int         column;  // 0-based
    std::string message;
    std::string file{};  // original source file; empty = same as compiled file
    int         severity{1}; // LSP DiagnosticSeverity: 1 error, 3 information
};

// One package import/export statement: `import pkgName::item`, `import pkgName::*`,
// `export pkgName::item`, or `export pkgName::*`.
struct ImportRecord {
    std::string pkgName;   // package being imported/exported
    std::string item;      // symbol name, or "*" for wildcard
    int         line{0};   // 1-based line in `file`
    std::string file{};    // empty = same as compiled file
    bool        isExport{false}; // true for `export`, false for plain `import`
    std::string scope{};   // enclosing scope chain ("" = compilation unit)
};

// One module/interface/program instantiation: `Foo u0 (...);` — a reference
// to a design unit that may or may not be declared anywhere yet known.
struct InstantiationRecord {
    std::string typeName;   // module/interface/program identifier being instantiated
    std::string instName;   // instance_identifier text (name_of_instance)
    int         line{0};    // 1-based line in `file`
    std::string file{};     // empty = same as compiled file
};

// One argument slot at a bare function/task call site (plan.md §6.23): a
// supplied positional expression, an elided positional slot (SV allows
// skipping a defaulted parameter by position while still supplying later
// ones, e.g. `foo(a, , c)`), or a named `.identifier(...)` connection --
// legal for plain function/task calls too, not just module port
// connections. `name` is populated only for Kind::Named.
struct CallArgSlot {
    enum class Kind { Positional, Elided, Named };
    Kind        kind;
    std::string name{};
};

// One bare (undotted) function/task call site: `my_func(a, .b(2))`.
// Dotted calls (`obj.method(...)`) are never recorded here -- see
// enterTf_call, src/compiler/sv_tree_walker.cpp. `args` is ordered exactly
// as written; positional/elided slots always precede any named ones (the
// grammar's own list_of_arguments rule never interleaves them).
struct CallRecord {
    std::string calleeName;
    // The whole qualifier of an explicitly `Class::`/`pkg::`-qualified call
    // (e.g. "type_id" for `type_id::create(...)`, "T::type_id" for the
    // doubly-qualified `T::type_id::create(...)` idiom, so resolution looks
    // for a type_id declared in T) -- empty for a genuinely unqualified
    // call. Retained (not discarded) so resolution can scope the lookup to
    // that exact name's own class hierarchy rather than searching the whole
    // database by bare name alone
    // (plan.md §6.26 -- this used to be dropped here, the root cause of
    // most of that section's disclosed false positives).
    std::string calleeScope;
    std::vector<CallArgSlot> args;
    int         line{0};    // 1-based line in `file`
    int         column{0};  // 0-based
    std::string file{};     // empty = same as compiled file
    // Where `calleeName` itself is (after any qualifier), same convention as
    // line/column -- what the callee is resolved at.
    int         nameLine{0};
    int         nameColumn{0};
};
