#pragma once
#include <string>
#include <vector>

// A cheap text pre-scan of SystemVerilog source (plan.md §6.34 S4), used
// only to order the background load: which units to compile first so the
// open documents' answers fill in soonest. No preprocessing, no parse; being
// wrong costs time, never correctness.
struct SourceScan {
    // Names declared at the top of a design element or class header:
    // `package p`, `class c`, `module m`, `interface i`, `program p`.
    std::vector<std::string> declares;
    // Names this text refers to: before `::`, after `import`, `extends` and
    // `implements`.
    std::vector<std::string> references;
    // The file names of its `include directives, as written. An `include
    // `MACRO (plan.md §6.36) is not seen: the scan doesn't expand macros,
    // which only costs ordering.
    std::vector<std::string> includes;
};

SourceScan scanSource(const std::string& text);

// `path`'s last path component.
std::string baseName(const std::string& path);
