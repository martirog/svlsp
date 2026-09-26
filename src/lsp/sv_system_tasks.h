#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Built-in system tasks and functions (`$display`, `$clog2`, ...) for
// signature help (plan.md §6.29 part C). They are part of the language and
// never declared anywhere a DB row could come from, so SignatureHelpProvider
// falls back to this table when a `$name(` call finds no Function/Task row.
//
// Disclosed uncertainty: every name/parameter list below is reconstructed
// from training-time familiarity with IEEE 1800-2017 Clauses 20/21, not
// verified against an LRM copy -- same posture as sv_builtin_methods.h
// (§6.13) and sv_keywords.h (§6.9).
//
// `params` is a ", "-separated list, rendered as-is:
//   - `[name]`  an optional parameter;
//   - `name...` a variadic tail (always last): activeParameter clamps to it
//     once the cursor is past the fixed parameters.
// An empty `params` means the task/function takes no arguments.

struct SystemTask {
    std::string_view name;
    std::string_view params;
    std::string_view doc;
};

inline constexpr SystemTask SYSTEM_TASKS[] = {
    // Display and formatting (Clause 21.2, 21.3.3)
    {"$display",   "args...", "Write arguments followed by a newline."},
    {"$displayb",  "args...", "$display with binary default radix."},
    {"$displayh",  "args...", "$display with hexadecimal default radix."},
    {"$displayo",  "args...", "$display with octal default radix."},
    {"$write",     "args...", "Write arguments without a trailing newline."},
    {"$writeb",    "args...", "$write with binary default radix."},
    {"$writeh",    "args...", "$write with hexadecimal default radix."},
    {"$writeo",    "args...", "$write with octal default radix."},
    {"$strobe",    "args...", "Write arguments at the end of the current time step."},
    {"$strobeb",   "args...", "$strobe with binary default radix."},
    {"$strobeh",   "args...", "$strobe with hexadecimal default radix."},
    {"$strobeo",   "args...", "$strobe with octal default radix."},
    {"$monitor",   "args...", "Write arguments whenever one of them changes."},
    {"$monitorb",  "args...", "$monitor with binary default radix."},
    {"$monitorh",  "args...", "$monitor with hexadecimal default radix."},
    {"$monitoro",  "args...", "$monitor with octal default radix."},
    {"$monitoron", "", "Re-enable $monitor."},
    {"$monitoroff", "", "Disable $monitor."},
    {"$sformatf",  "format, args...", "Return a string formatted like $display."},
    {"$sformat",   "output_var, format, args...", "Format a string into output_var."},
    {"$swrite",    "output_var, args...", "Write arguments into the string output_var."},
    {"$swriteb",   "output_var, args...", "$swrite with binary default radix."},
    {"$swriteh",   "output_var, args...", "$swrite with hexadecimal default radix."},
    {"$swriteo",   "output_var, args...", "$swrite with octal default radix."},
    {"$psprintf",  "format, args...", "Return a formatted string (non-standard alias of $sformatf)."},

    // File I/O (Clause 21.3)
    {"$fopen",     "filename, [mode]", "Open a file; returns a file descriptor (0 on failure)."},
    {"$fclose",    "fd", "Close a file."},
    {"$fdisplay",  "fd, args...", "$display to a file."},
    {"$fdisplayb", "fd, args...", "$fdisplay with binary default radix."},
    {"$fdisplayh", "fd, args...", "$fdisplay with hexadecimal default radix."},
    {"$fdisplayo", "fd, args...", "$fdisplay with octal default radix."},
    {"$fwrite",    "fd, args...", "$write to a file."},
    {"$fwriteb",   "fd, args...", "$fwrite with binary default radix."},
    {"$fwriteh",   "fd, args...", "$fwrite with hexadecimal default radix."},
    {"$fwriteo",   "fd, args...", "$fwrite with octal default radix."},
    {"$fstrobe",   "fd, args...", "$strobe to a file."},
    {"$fmonitor",  "fd, args...", "$monitor to a file."},
    {"$fflush",    "[fd]", "Flush a file's buffered output (all files if omitted)."},
    {"$fgetc",     "fd", "Read one character; returns EOF (-1) on end of file."},
    {"$ungetc",    "c, fd", "Push a character back onto a file."},
    {"$fgets",     "str, fd", "Read a line into str; returns the number of characters read."},
    {"$fscanf",    "fd, format, args...", "Read formatted data from a file."},
    {"$sscanf",    "str, format, args...", "Read formatted data from a string."},
    {"$fread",     "dest, fd, [start], [count]", "Read binary data from a file."},
    {"$ftell",     "fd", "Return the current file offset."},
    {"$fseek",     "fd, offset, operation", "Set the file offset (operation: 0 set, 1 current, 2 end)."},
    {"$rewind",    "fd", "Set the file offset to the beginning."},
    {"$feof",      "fd", "Return nonzero at end of file."},
    {"$ferror",    "fd, str", "Return the last I/O error code and its message in str."},
    {"$readmemb",  "filename, memory, [start_addr], [finish_addr]", "Load a memory from a binary text file."},
    {"$readmemh",  "filename, memory, [start_addr], [finish_addr]", "Load a memory from a hexadecimal text file."},
    {"$writememb", "filename, memory, [start_addr], [finish_addr]", "Write a memory to a binary text file."},
    {"$writememh", "filename, memory, [start_addr], [finish_addr]", "Write a memory to a hexadecimal text file."},

    // Simulation control and severity (Clause 20.2, 20.10)
    {"$finish",    "[finish_number]", "End the simulation (finish_number 0, 1 or 2 sets diagnostics)."},
    {"$stop",      "[finish_number]", "Suspend the simulation."},
    {"$exit",      "", "Wait for all program blocks to complete, then end the simulation."},
    {"$fatal",     "[finish_number], format, args...", "Report a fatal error and end the simulation."},
    {"$error",     "format, args...", "Report a run-time error."},
    {"$warning",   "format, args...", "Report a run-time warning."},
    {"$info",      "format, args...", "Report an informational message."},

    // Time (Clause 20.3, 20.4)
    {"$time",      "", "Current simulation time as a 64-bit integer, in the module's time unit."},
    {"$stime",     "", "Current simulation time as a 32-bit unsigned integer."},
    {"$realtime",  "", "Current simulation time as a real number."},
    {"$timeformat", "[units_number], [precision_number], [suffix_string], [minimum_field_width]",
                   "Set how %t formats time values."},
    {"$printtimescale", "[hierarchical_identifier]", "Display a module's time unit and precision."},

    // Conversion and type (Clause 20.5, 20.6, 6.24.2)
    {"$cast",       "dest_var, source_exp", "Dynamic cast; as a function returns 1 on success, 0 on failure."},
    {"$signed",     "expression", "Return the value interpreted as signed."},
    {"$unsigned",   "expression", "Return the value interpreted as unsigned."},
    {"$itor",       "int_val", "Convert an integer to a real."},
    {"$rtoi",       "real_val", "Convert a real to an integer by truncation."},
    {"$bitstoreal", "bit_val", "Convert a 64-bit pattern to a real."},
    {"$realtobits", "real_val", "Convert a real to its 64-bit pattern."},
    {"$bitstoshortreal", "bit_val", "Convert a 32-bit pattern to a shortreal."},
    {"$shortrealtobits", "shortreal_val", "Convert a shortreal to its 32-bit pattern."},
    {"$typename",   "expression_or_type", "Return the type's name as a string."},
    {"$isunbounded", "constant_expression", "Return 1 if the argument is $."},

    // Math (Clause 20.8)
    {"$clog2",  "n", "Ceiling of the base-2 logarithm."},
    {"$ln",     "x", "Natural logarithm."},
    {"$log10",  "x", "Base-10 logarithm."},
    {"$exp",    "x", "e raised to the power x."},
    {"$sqrt",   "x", "Square root."},
    {"$pow",    "x, y", "x raised to the power y."},
    {"$floor",  "x", "Floor."},
    {"$ceil",   "x", "Ceiling."},
    {"$sin",    "x", "Sine."},
    {"$cos",    "x", "Cosine."},
    {"$tan",    "x", "Tangent."},
    {"$asin",   "x", "Arc-sine."},
    {"$acos",   "x", "Arc-cosine."},
    {"$atan",   "x", "Arc-tangent."},
    {"$atan2",  "y, x", "Arc-tangent of y/x."},
    {"$hypot",  "x, y", "sqrt(x*x + y*y)."},
    {"$sinh",   "x", "Hyperbolic sine."},
    {"$cosh",   "x", "Hyperbolic cosine."},
    {"$tanh",   "x", "Hyperbolic tangent."},
    {"$asinh",  "x", "Hyperbolic arc-sine."},
    {"$acosh",  "x", "Hyperbolic arc-cosine."},
    {"$atanh",  "x", "Hyperbolic arc-tangent."},

    // Bit-vector and array query (Clause 20.6.2, 20.7, 20.9)
    {"$bits",       "expression_or_type", "Number of bits needed to hold the value or type."},
    {"$countones",  "expression", "Number of bits set to 1."},
    {"$countbits",  "expression, control_bit, control_bits...", "Number of bits matching any of the control bits."},
    {"$onehot",     "expression", "1 if exactly one bit is 1."},
    {"$onehot0",    "expression", "1 if at most one bit is 1."},
    {"$isunknown",  "expression", "1 if any bit is X or Z."},
    {"$dimensions", "array_or_type", "Total number of dimensions."},
    {"$unpacked_dimensions", "array_or_type", "Number of unpacked dimensions."},
    {"$size",       "array_or_type, [dimension]", "Number of elements in a dimension."},
    {"$left",       "array_or_type, [dimension]", "Left bound of a dimension."},
    {"$right",      "array_or_type, [dimension]", "Right bound of a dimension."},
    {"$low",        "array_or_type, [dimension]", "Lower bound of a dimension."},
    {"$high",       "array_or_type, [dimension]", "Upper bound of a dimension."},
    {"$increment",  "array_or_type, [dimension]", "1 if left >= right, else -1."},

    // Random numbers and distributions (Clause 18.13, 20.15)
    {"$urandom",       "[seed]", "Unsigned 32-bit pseudo-random number."},
    {"$urandom_range", "maxval, [minval]", "Unsigned pseudo-random number in [minval, maxval]."},
    {"$random",        "[seed]", "Signed 32-bit pseudo-random number."},
    {"$dist_uniform",     "seed, start, end", "Uniform distribution."},
    {"$dist_normal",      "seed, mean, standard_deviation", "Normal distribution."},
    {"$dist_exponential", "seed, mean", "Exponential distribution."},
    {"$dist_poisson",     "seed, mean", "Poisson distribution."},
    {"$dist_chi_square",  "seed, degree_of_freedom", "Chi-square distribution."},
    {"$dist_t",           "seed, degree_of_freedom", "Student's t distribution."},
    {"$dist_erlang",      "seed, k_stage, mean", "Erlang distribution."},

    // Plusargs (Clause 21.6)
    {"$test$plusargs",  "string", "1 if a +plusarg starting with string was given."},
    {"$value$plusargs", "user_string, variable", "Parse a +plusarg's value into variable; 1 if found."},

    // Assertion control (Clause 20.12, 20.11)
    {"$asserton",   "[levels], [list_of_scopes_or_assertions...]", "Enable assertions."},
    {"$assertoff",  "[levels], [list_of_scopes_or_assertions...]", "Disable assertions."},
    {"$assertkill", "[levels], [list_of_scopes_or_assertions...]", "Abort and disable assertions."},
    {"$assertcontrol", "control_type, [assertion_type], [directive_type], [levels], [list...]",
                       "General assertion control."},
    {"$rose",     "expression, [clocking_event]", "1 if the expression's LSB rose since the last clock tick."},
    {"$fell",     "expression, [clocking_event]", "1 if the expression's LSB fell since the last clock tick."},
    {"$stable",   "expression, [clocking_event]", "1 if the expression didn't change since the last clock tick."},
    {"$changed",  "expression, [clocking_event]", "1 if the expression changed since the last clock tick."},
    {"$past",     "expression, [number_of_ticks], [expression2], [clocking_event]",
                  "The expression's value number_of_ticks clock ticks ago."},
    {"$sampled",  "expression", "The expression's sampled value."},

    // Coverage (Clause 19.8)
    {"$get_coverage", "", "Overall coverage of all coverage group types (0-100)."},
    {"$set_coverage_db_name", "filename", "Set the coverage database file name."},
    {"$load_coverage_db", "filename", "Load cumulative coverage from a database file."},
};

inline const SystemTask* findSystemTask(std::string_view name)
{
    for (const auto& t : SYSTEM_TASKS)
        if (t.name == name) return &t;
    return nullptr;
}

// Splits a SystemTask's `params` on ", ". Empty for a no-argument task.
inline std::vector<std::string> systemTaskParams(const SystemTask& task)
{
    std::vector<std::string> out;
    if (task.params.empty()) return out;
    size_t start = 0;
    for (;;) {
        size_t sep = task.params.find(", ", start);
        out.emplace_back(task.params.substr(start, sep == std::string_view::npos
                                                       ? std::string_view::npos
                                                       : sep - start));
        if (sep == std::string_view::npos) break;
        start = sep + 2;
    }
    return out;
}
