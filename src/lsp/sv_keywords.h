#pragma once
#include <array>
#include <string>
#include <string_view>

// SystemVerilog reserved-word table for keyword completion (plan.md §6.9,
// extended with context-legality per the user's explicit "can't nest
// modules" requirement).
//
// Source: every quoted identifier-syntax literal token in grammar/Sv.g4
// (script-extracted, deduped), which is essentially all of IEEE 1800-2017
// Annex B minus backtick-directive words (`define/`ifdef/... are lexically
// distinct macro tokens handled by the preprocessor, not this grammar).
//
// Context model: SymbolDatabase::scopeKindAtPosition (src/db/symbol_database.h)
// reports the innermost *declaration scope* at a cursor -- one of Module,
// Interface, Program, Package, Class, Function, Task, or "" (top level /
// compilation unit). That is the only position information this codebase
// tracks anywhere (nothing records "inside an always_ff block" vs "directly
// in the module body" -- both just report scope kind Module, since
// always/initial/begin/if aren't ParseRecordKinds). So legality here is
// enforced at exactly that grain: a design-unit keyword like `module` is
// correctly rejected once you're inside *any* other declaration scope (the
// user's own example -- modules/interfaces/programs/packages/primitives
// cannot nest in SV), but this does NOT attempt finer statement/block-level
// precision (e.g. it can't tell "directly in a module body, no enclosing
// always block" from "inside one" -- both are scope kind Module).
//
// Per-keyword context assignment is grounded in Sv.g4's own rule structure
// plus SV domain knowledge, not a verified line-by-line LRM audit (~254
// keywords is not practical to audit that way) -- same disclosed-uncertainty
// posture as plan.md §6.13's built-in method lists. Where a keyword's real
// legality spans an unmodeled sub-context (e.g. coverpoint/bins are only
// legal *inside* a covergroup body, which isn't its own tracked scope kind),
// it is bucketed with its nearest enclosing trackable context rather than
// dropped -- false positives (occasionally offered where not technically
// legal) are the accepted failure mode here, never false negatives (a real
// keyword silently never offered).
//
// Note: scopeKindAtPosition never reports "Program" today -- it mirrors
// scopeAtPosition's existing kind list, which doesn't track Program as a
// scope kind either (a pre-existing gap, not introduced here). The Program
// bit below is still assigned wherever a keyword is legal inside a program
// body, for correctness/future-readiness, but has no observable effect
// until that pre-existing gap is closed elsewhere.

enum KeywordContext : unsigned {
    KwTopLevel  = 1u << 0, // scope kind "" (top level / compilation unit)
    KwModule    = 1u << 1,
    KwInterface = 1u << 2,
    KwProgram   = 1u << 3,
    KwPackage   = 1u << 4,
    KwClass     = 1u << 5,
    KwFunction  = 1u << 6,
    KwTask      = 1u << 7,
};

inline constexpr unsigned KW_DECL_ALL =
    KwTopLevel | KwModule | KwInterface | KwProgram | KwPackage | KwClass | KwFunction | KwTask;
inline constexpr unsigned KW_DESIGN_BODY   = KwModule | KwInterface | KwProgram;
inline constexpr unsigned KW_SUBROUTINE    = KwFunction | KwTask;
inline constexpr unsigned KW_PROC_STMT     = KW_DESIGN_BODY | KW_SUBROUTINE;
inline constexpr unsigned KW_CLASS_FN_TASK = KwClass | KwFunction | KwTask;
inline constexpr unsigned KW_CLASS_DECL_CONTAINERS =
    KwTopLevel | KwModule | KwInterface | KwProgram | KwPackage;
inline constexpr unsigned KW_FUNC_TASK_DECL_CONTAINERS = KW_CLASS_DECL_CONTAINERS | KwClass;
inline constexpr unsigned KW_EXPR_CTX      = KW_DESIGN_BODY | KW_CLASS_FN_TASK;
inline constexpr unsigned KW_PORT_DIR      = KW_DESIGN_BODY | KW_SUBROUTINE;
inline constexpr unsigned KW_NET_TYPE      = KwTopLevel | KW_DESIGN_BODY;
inline constexpr unsigned KW_MODULE_ONLY   = KwModule;
inline constexpr unsigned KW_TOP_ONLY      = KwTopLevel;

struct SvKeyword {
    std::string_view text;
    unsigned         contexts;
};

// Maps a SymbolDatabase::scopeKindAtPosition() result ("", "Module",
// "Interface", "Program", "Package", "Class", "Function", "Task") to one
// KeywordContext bit. Any unrecognized/future kind falls back to KwTopLevel
// rather than silently matching everything.
inline unsigned contextBitFor(const std::string& scopeKind)
{
    if (scopeKind == "Module")    return KwModule;
    if (scopeKind == "Interface") return KwInterface;
    if (scopeKind == "Program")   return KwProgram;
    if (scopeKind == "Package")   return KwPackage;
    if (scopeKind == "Class")     return KwClass;
    if (scopeKind == "Function")  return KwFunction;
    if (scopeKind == "Task")      return KwTask;
    return KwTopLevel;
}

// clang-format off
inline constexpr std::array<SvKeyword, 254> SV_KEYWORDS = {{
    // --- Design-unit declarations: cannot nest (the user's own example) ---
    {"module",         KW_TOP_ONLY},
    // endmodule closes a body while still inside it -- see the end* comment
    // just below.
    {"endmodule",      KwModule},
    {"macromodule",    KW_TOP_ONLY},
    // The "end*" keywords close a body while still *inside* it -- the
    // cursor's tracked scope kind at that point is the body being closed,
    // not wherever the opener itself was legal to type. Unlike the openers
    // above, these are scoped to their own body kind, not KW_TOP_ONLY.
    {"endinterface",   KwInterface},
    {"program",        KW_TOP_ONLY},
    // scopeKindAtPosition never reports "Program" today (a pre-existing gap
    // in the underlying query shared with scopeAtPosition -- see the header
    // comment above); KwProgram is still the semantically correct bucket,
    // just unreachable until that gap closes elsewhere.
    {"endprogram",     KwProgram},
    {"package",        KW_TOP_ONLY},
    {"endpackage",     KwPackage},
    {"primitive",      KW_TOP_ONLY},
    // Primitive isn't a tracked ParseRecordKind at all, so there's no
    // "Primitive" scope kind to target -- TopLevel is the best achievable
    // fallback here (same disclosed imprecision as table/endtable below).
    {"endprimitive",   KW_TOP_ONLY},
    {"config",         KW_TOP_ONLY},
    // Config isn't a tracked ParseRecordKind either -- same fallback reasoning.
    {"endconfig",      KW_TOP_ONLY},
    // "interface" also opens `virtual interface IfcName` as a data type
    // reference (legal wherever a data type is), so unlike endinterface it
    // is NOT top-level-only.
    {"interface",      KW_DECL_ALL},

    // --- Class declaration ---
    {"class",          KW_CLASS_DECL_CONTAINERS},
    {"endclass",       KwClass},
    // extends/implements are typed on the same line as "class Foo", which
    // can resolve as either still-TopLevel-ish (file not yet reparsed) or
    // already Class (self-referential scope match against the existing
    // parse) depending on timing -- cover both.
    {"extends",        KW_CLASS_DECL_CONTAINERS | KwClass},
    {"implements",     KW_CLASS_DECL_CONTAINERS | KwClass},

    // --- Class-member-only qualifiers ---
    {"constraint",     KwClass},
    {"local",          KwClass},
    {"protected",      KwClass},
    {"rand",           KwClass},
    {"randc",          KwClass},
    {"pure",           KwClass},
    // "virtual" also opens `virtual interface IfcName` (data-type context,
    // like "interface" above), so it spans everywhere a data type can
    // appear, not just class member/method modifiers.
    {"virtual",        KW_DECL_ALL},
    // "extern" also forward-declares a design unit at top level (`extern
    // module ...`) and DPI import qualifiers, in addition to extern class
    // method prototypes.
    {"extern",         KW_DECL_ALL},

    // --- Function/Task declarations (SV has no nested subroutines) ---
    {"function",       KW_FUNC_TASK_DECL_CONTAINERS},
    // endfunction/endtask close a body while still inside it -- see the
    // end* comment above the design-unit bucket.
    {"endfunction",    KwFunction},
    {"task",           KW_FUNC_TASK_DECL_CONTAINERS},
    {"endtask",        KwTask},
    // DPI import qualifier ("import \"DPI-C\" context function ...") --
    // legal wherever a function/task prototype can appear.
    {"context",        KW_FUNC_TASK_DECL_CONTAINERS},

    // --- Subroutine-body-only ---
    {"return",         KW_SUBROUTINE},

    // --- Procedural / statement keywords ---
    {"if",             KW_PROC_STMT},
    {"else",           KW_PROC_STMT},
    {"case",           KW_PROC_STMT},
    {"casex",          KW_PROC_STMT},
    {"casez",          KW_PROC_STMT},
    {"endcase",        KW_PROC_STMT},
    {"default",        KW_DECL_ALL}, // also default port connection, default clocking
    {"for",            KW_PROC_STMT},
    {"foreach",        KW_PROC_STMT},
    {"while",          KW_PROC_STMT},
    {"do",             KW_PROC_STMT},
    {"repeat",         KW_PROC_STMT},
    {"forever",        KW_PROC_STMT},
    {"begin",          KW_PROC_STMT},
    {"end",            KW_PROC_STMT},
    {"fork",           KW_PROC_STMT},
    {"join",           KW_PROC_STMT},
    {"join_any",       KW_PROC_STMT},
    {"join_none",      KW_PROC_STMT},
    {"forkjoin",       KW_PROC_STMT},
    {"disable",        KW_PROC_STMT},
    {"break",          KW_PROC_STMT},
    {"continue",       KW_PROC_STMT},
    {"wait",           KW_PROC_STMT},
    {"wait_order",     KW_PROC_STMT},
    {"assert",         KW_PROC_STMT},
    {"assume",         KW_PROC_STMT},
    {"cover",          KW_PROC_STMT},
    {"expect",         KW_PROC_STMT},
    {"priority",       KW_PROC_STMT},
    {"unique",         KW_PROC_STMT},
    {"unique0",        KW_PROC_STMT},
    {"randcase",       KW_PROC_STMT},
    {"randsequence",   KW_PROC_STMT},
    {"deassign",       KW_PROC_STMT},
    {"force",          KW_PROC_STMT},
    {"release",        KW_PROC_STMT},
    {"restrict",       KW_PROC_STMT},
    {"new",            KW_DECL_ALL}, // object/array construction, used broadly

    // --- Procedural-block starters (module/interface/program items) ---
    {"always",         KW_DESIGN_BODY},
    {"always_comb",    KW_DESIGN_BODY},
    {"always_ff",      KW_DESIGN_BODY},
    {"always_latch",   KW_DESIGN_BODY},
    {"initial",        KW_DESIGN_BODY},
    {"final",          KW_DESIGN_BODY},
    {"assign",         KW_DESIGN_BODY},
    {"generate",       KW_DESIGN_BODY},
    {"endgenerate",    KW_DESIGN_BODY},
    {"genvar",         KW_DESIGN_BODY},
    {"alias",          KW_DESIGN_BODY},
    {"global",         KwModule | KwInterface}, // global clocking

    // --- Event-control keywords (procedural sensitivity lists; not legal
    //     in plain functions, only tasks) ---
    {"posedge",        KW_DESIGN_BODY | KwTask},
    {"negedge",        KW_DESIGN_BODY | KwTask},
    {"edge",           KW_DESIGN_BODY | KwTask},

    // --- Data types & general declaration keywords (legal wherever a
    //     declaration or local variable can occur) ---
    {"logic",          KW_DECL_ALL},
    {"reg",            KW_DECL_ALL},
    {"bit",            KW_DECL_ALL},
    {"byte",           KW_DECL_ALL},
    {"shortint",       KW_DECL_ALL},
    {"int",            KW_DECL_ALL},
    {"longint",        KW_DECL_ALL},
    {"integer",        KW_DECL_ALL},
    {"time",           KW_DECL_ALL},
    {"real",           KW_DECL_ALL},
    {"shortreal",      KW_DECL_ALL},
    {"realtime",       KW_DECL_ALL},
    {"string",         KW_DECL_ALL},
    {"chandle",        KW_DECL_ALL},
    {"event",          KW_DECL_ALL},
    {"void",           KW_DECL_ALL},
    {"enum",           KW_DECL_ALL},
    {"struct",         KW_DECL_ALL},
    {"union",          KW_DECL_ALL},
    {"typedef",        KW_DECL_ALL},
    {"parameter",      KW_DECL_ALL},
    {"localparam",     KW_DECL_ALL},
    {"signed",         KW_DECL_ALL},
    {"unsigned",       KW_DECL_ALL},
    {"packed",         KW_DECL_ALL},
    {"const",          KW_DECL_ALL},
    {"static",         KW_DECL_ALL},
    {"automatic",      KW_DECL_ALL},
    {"var",            KW_DECL_ALL},
    {"null",           KW_DECL_ALL},
    {"type",           KW_DECL_ALL},
    {"untyped",        KW_DECL_ALL},
    {"std",            KW_DECL_ALL},
    {"let",            KW_DECL_ALL},

    // --- Net-type keywords (nets: not declarable inside class/function/task) ---
    {"wire",           KW_NET_TYPE},
    {"tri",            KW_NET_TYPE},
    {"tri0",           KW_NET_TYPE},
    {"tri1",           KW_NET_TYPE},
    {"triand",         KW_NET_TYPE},
    {"trior",          KW_NET_TYPE},
    {"trireg",         KW_NET_TYPE},
    {"wand",           KW_NET_TYPE},
    {"wor",            KW_NET_TYPE},
    {"supply0",        KW_NET_TYPE},
    {"supply1",        KW_NET_TYPE},
    {"uwire",          KW_NET_TYPE},
    {"interconnect",   KW_NET_TYPE},
    {"nettype",        KW_NET_TYPE},

    // --- Port / subroutine-argument direction ---
    {"input",          KW_PORT_DIR},
    {"output",         KW_PORT_DIR},
    {"inout",          KW_PORT_DIR},
    {"ref",            KW_PORT_DIR},

    // --- Import/export ---
    {"import",         KW_DECL_ALL},
    {"export",         KW_DECL_ALL},

    // --- bind directive ---
    {"bind",           KwTopLevel | KW_DESIGN_BODY},
    {"instance",       KwTopLevel | KW_DESIGN_BODY},
    {"defparam",       KW_DESIGN_BODY},

    // --- Interface-only ---
    {"modport",        KwInterface},

    // --- Module-only: specify blocks, gate primitives ---
    {"specify",        KW_MODULE_ONLY},
    {"endspecify",     KW_MODULE_ONLY},
    {"specparam",      KW_MODULE_ONLY},
    {"and",            KW_MODULE_ONLY},
    {"or",             KW_MODULE_ONLY},
    {"nand",           KW_MODULE_ONLY},
    {"nor",            KW_MODULE_ONLY},
    {"xor",            KW_MODULE_ONLY},
    {"xnor",           KW_MODULE_ONLY},
    {"not",            KW_MODULE_ONLY},
    {"buf",            KW_MODULE_ONLY},
    {"bufif0",         KW_MODULE_ONLY},
    {"bufif1",         KW_MODULE_ONLY},
    {"notif0",         KW_MODULE_ONLY},
    {"notif1",         KW_MODULE_ONLY},
    {"nmos",           KW_MODULE_ONLY},
    {"pmos",           KW_MODULE_ONLY},
    {"cmos",           KW_MODULE_ONLY},
    {"rnmos",          KW_MODULE_ONLY},
    {"rpmos",          KW_MODULE_ONLY},
    {"rcmos",          KW_MODULE_ONLY},
    {"tran",           KW_MODULE_ONLY},
    {"tranif0",        KW_MODULE_ONLY},
    {"tranif1",        KW_MODULE_ONLY},
    {"rtran",          KW_MODULE_ONLY},
    {"rtranif0",       KW_MODULE_ONLY},
    {"rtranif1",       KW_MODULE_ONLY},
    {"pulldown",       KW_MODULE_ONLY},
    {"pullup",         KW_MODULE_ONLY},
    {"showcancelled",   KW_MODULE_ONLY},
    {"noshowcancelled", KW_MODULE_ONLY},
    {"pulsestyle_ondetect", KW_MODULE_ONLY},
    {"pulsestyle_onevent",  KW_MODULE_ONLY},

    // --- Net/gate drive-strength & charge-strength modifiers ---
    {"strong0",        KW_NET_TYPE},
    {"strong1",        KW_NET_TYPE},
    {"weak0",          KW_NET_TYPE},
    {"weak1",          KW_NET_TYPE},
    {"strong",         KW_NET_TYPE},
    {"weak",           KW_NET_TYPE},
    {"pull0",          KW_NET_TYPE},
    {"pull1",          KW_NET_TYPE},
    {"highz0",         KW_NET_TYPE},
    {"highz1",         KW_NET_TYPE},
    {"small",          KW_NET_TYPE},
    {"medium",         KW_NET_TYPE},
    {"large",          KW_NET_TYPE},
    {"scalared",       KW_NET_TYPE},
    {"vectored",       KW_NET_TYPE},

    // --- this / super ---
    {"this",           KW_CLASS_FN_TASK},
    {"super",          KW_CLASS_FN_TASK},

    // --- Expression / constraint / assertion-temporal operators ---
    {"inside",         KW_EXPR_CTX},
    {"dist",           KW_EXPR_CTX},
    {"with",           KW_EXPR_CTX},
    {"within",         KW_EXPR_CTX},
    {"intersect",      KW_EXPR_CTX},
    {"throughout",     KW_EXPR_CTX},
    {"matches",        KW_EXPR_CTX},
    {"tagged",         KW_EXPR_CTX},
    {"wildcard",       KW_EXPR_CTX},
    {"solve",          KW_EXPR_CTX},
    {"soft",           KW_EXPR_CTX},
    {"before",         KW_EXPR_CTX},
    {"iff",            KW_EXPR_CTX},
    {"ifnone",         KW_EXPR_CTX},
    {"first_match",    KW_EXPR_CTX},
    {"until",          KW_EXPR_CTX},
    {"until_with",     KW_EXPR_CTX},
    {"s_until",        KW_EXPR_CTX},
    {"s_until_with",   KW_EXPR_CTX},
    {"eventually",     KW_EXPR_CTX},
    {"s_eventually",   KW_EXPR_CTX},
    {"s_always",       KW_EXPR_CTX},
    {"nexttime",       KW_EXPR_CTX},
    {"s_nexttime",     KW_EXPR_CTX},
    {"implies",        KW_EXPR_CTX},
    {"accept_on",      KW_EXPR_CTX},
    {"reject_on",      KW_EXPR_CTX},
    {"sync_accept_on", KW_EXPR_CTX},
    {"sync_reject_on", KW_EXPR_CTX},

    // --- Randomization-family (obj.randomize() call site + declarations) ---
    {"randomize",      KW_CLASS_FN_TASK},

    // --- Sequence / property declarations (module/interface/program/
    //     package items; not inside class/function bodies) ---
    {"sequence",       KW_CLASS_DECL_CONTAINERS},
    {"endsequence",    KW_CLASS_DECL_CONTAINERS | KW_PROC_STMT}, // also closes randsequence
    {"property",       KW_CLASS_DECL_CONTAINERS},
    {"endproperty",    KW_CLASS_DECL_CONTAINERS},
    {"clocking",       KW_DESIGN_BODY},
    {"endclocking",    KW_DESIGN_BODY},

    // --- Checker (LRM: unlike module/interface/program/package, checkers
    //     CAN nest inside those constructs and inside generate blocks --
    //     flagged uncertain, not individually verified against the LRM) ---
    {"checker",        KW_CLASS_DECL_CONTAINERS},
    {"endchecker",     KW_CLASS_DECL_CONTAINERS},

    // --- Covergroup / coverpoint (a covergroup type can be embedded in a
    //     class -- common UVM pattern -- as well as module/package level;
    //     coverpoint/bins/cross/binsof are only legal *inside* a covergroup
    //     body, which isn't its own tracked scope kind, so they're bucketed
    //     with the same mask as covergroup itself -- a disclosed imprecision) ---
    {"covergroup",     KW_FUNC_TASK_DECL_CONTAINERS},
    {"endgroup",       KW_FUNC_TASK_DECL_CONTAINERS},
    {"coverpoint",     KW_FUNC_TASK_DECL_CONTAINERS},
    {"cross",          KW_FUNC_TASK_DECL_CONTAINERS},
    {"bins",           KW_FUNC_TASK_DECL_CONTAINERS},
    {"ignore_bins",    KW_FUNC_TASK_DECL_CONTAINERS},
    {"illegal_bins",   KW_FUNC_TASK_DECL_CONTAINERS},
    {"binsof",         KW_FUNC_TASK_DECL_CONTAINERS},

    // --- Library / config-map (top-level file constructs) ---
    {"use",            KW_TOP_ONLY},
    {"liblist",        KW_TOP_ONLY},
    {"library",        KW_TOP_ONLY},
    {"cell",           KW_TOP_ONLY},
    {"design",         KW_TOP_ONLY},
    {"include",        KW_TOP_ONLY},
    {"table",          KW_TOP_ONLY},
    {"endtable",       KW_TOP_ONLY},

    // --- Timescale / time-unit declarations (legal at compilation-unit
    //     scope and as the first item in a module/interface/program body) ---
    {"timeunit",       KwTopLevel | KW_DESIGN_BODY},
    {"timeprecision",  KwTopLevel | KW_DESIGN_BODY},
    {"fs",             KW_DECL_ALL},
    {"ps",             KW_DECL_ALL},
    {"ns",             KW_DECL_ALL},
    {"us",             KW_DECL_ALL},
    {"ms",             KW_DECL_ALL},
}};
// clang-format on
