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
};

struct ParseRecord {
    ParseRecordKind kind;
    std::string     name;
    int             line;   // 1-based
    int             column; // 0-based
};
