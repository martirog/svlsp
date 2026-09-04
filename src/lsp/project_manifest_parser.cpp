#include "lsp/project_manifest_parser.h"
#include <lsp/json/json.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

std::string resolvePath(const std::string& baseDir, const std::string& raw) {
    fs::path p(raw);
    if (p.is_absolute()) return p.lexically_normal().string();
    return (fs::path(baseDir) / p).lexically_normal().string();
}

// Reads a "field": [ "a", "b" ] array of strings. Throws if `field` is
// present but isn't an array of strings.
std::vector<std::string> readStringArray(const lsp::json::Object& obj, const std::string& field,
                                          const std::string& path) {
    std::vector<std::string> result;
    const lsp::json::Value* v = obj.find(field);
    if (!v) return result;
    if (!v->isArray()) {
        throw std::runtime_error(path + ": \"" + field + "\" must be an array of strings");
    }
    for (const auto& item : v->array()) {
        if (!item.isString()) {
            throw std::runtime_error(path + ": \"" + field + "\" must be an array of strings");
        }
        result.push_back(item.string());
    }
    return result;
}

// Reads "libraryDbSources": [ {"config": "...", "cache": "..."}, ... ].
// Throws if the field is present but isn't an array of {config, cache}
// string-valued objects.
std::vector<LibraryDbSource> readLibraryDbSources(
    const lsp::json::Object& obj, const std::string& baseDir, const std::string& path) {
    std::vector<LibraryDbSource> result;
    const lsp::json::Value* v = obj.find("libraryDbSources");
    if (!v) return result;
    if (!v->isArray()) {
        throw std::runtime_error(path + ": \"libraryDbSources\" must be an array");
    }
    for (const auto& item : v->array()) {
        if (!item.isObject()) {
            throw std::runtime_error(
                path + ": \"libraryDbSources\" entries must be objects");
        }
        const lsp::json::Object& entry = item.object();
        const lsp::json::Value* config = entry.find("config");
        const lsp::json::Value* cache  = entry.find("cache");
        if (!config || !config->isString() || !cache || !cache->isString()) {
            throw std::runtime_error(
                path + ": each \"libraryDbSources\" entry must have string "
                       "\"config\" and \"cache\" fields");
        }
        result.push_back({resolvePath(baseDir, config->string()),
                          resolvePath(baseDir, cache->string())});
    }
    return result;
}

} // namespace

ProjectConfig ProjectManifestParser::parse(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(path + ": cannot open project manifest");
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    lsp::json::Value root;
    try {
        root = lsp::json::parse(ss.str());
    } catch (const lsp::json::Error& e) {
        throw std::runtime_error(path + ": " + std::string(e.what()));
    }
    if (!root.isObject()) {
        throw std::runtime_error(path + ": manifest root must be a JSON object");
    }
    const lsp::json::Object& obj = root.object();

    std::string baseDir = fs::path(path).parent_path().lexically_normal().string();
    if (baseDir.empty()) baseDir = ".";

    ProjectConfig config;

    for (const auto& raw : readStringArray(obj, "files", path))
        config.files.push_back(resolvePath(baseDir, raw));
    for (const auto& raw : readStringArray(obj, "includeDirs", path))
        config.includeDirs.push_back(resolvePath(baseDir, raw));
    for (const auto& raw : readStringArray(obj, "libraryDirs", path))
        config.libraryDirs.push_back(resolvePath(baseDir, raw));
    for (const auto& raw : readStringArray(obj, "libraryFiles", path))
        config.libraryFiles.push_back(resolvePath(baseDir, raw));
    for (const auto& raw : readStringArray(obj, "libExtensions", path))
        config.libExtensions.push_back(raw);
    for (const auto& raw : readStringArray(obj, "libraryDbs", path))
        config.libraryDbs.push_back(resolvePath(baseDir, raw));
    config.libraryDbSources = readLibraryDbSources(obj, baseDir, path);

    if (const lsp::json::Value* v = obj.find("defines")) {
        if (!v->isObject()) {
            throw std::runtime_error(path + ": \"defines\" must be an object");
        }
        for (const auto& [name, value] : v->object().keyValueMap()) {
            if (!value.isString()) {
                throw std::runtime_error(path + ": \"defines." + name + "\" must be a string");
            }
            config.defines[name] = value.string();
        }
    }

    if (const lsp::json::Value* v = obj.find("top")) {
        if (!v->isString()) {
            throw std::runtime_error(path + ": \"top\" must be a string");
        }
        config.topModule = v->string();
    }

    if (const lsp::json::Value* v = obj.find("mode")) {
        if (!v->isString()) {
            throw std::runtime_error(path + ": \"mode\" must be a string");
        }
        const std::string& mode = v->string();
        if (mode == "sv") config.mode = SvLanguageMode::SystemVerilog;
        else if (mode == "v95") config.mode = SvLanguageMode::Verilog95;
        else throw std::runtime_error(path + ": \"mode\" must be \"sv\" or \"v95\", got \"" + mode + "\"");
    }

    return config;
}
