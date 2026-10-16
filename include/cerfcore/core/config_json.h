#pragma once

#include "host_fs_path.h"
#include "log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

[[noreturn]] inline void CfgFatal(const std::string& path,
                                  const std::string& msg) {
    LOG(Caution, "FATAL: '%s' %s\n", path.c_str(), msg.c_str());
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

inline nlohmann::json CfgReadJsonFile(const std::string& path) {
    std::ifstream f(HostFsPath(path));
    if (!f.is_open()) return nlohmann::json();
    nlohmann::json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        CfgFatal(path, std::string("JSON parse error: ") + e.what());
    }
    if (!j.is_object())
        CfgFatal(path, "top-level value must be a JSON object");
    return j;
}

inline nlohmann::json CfgReadLoggedJsonFile(const std::string& path) {
    nlohmann::json j = CfgReadJsonFile(path);
    if (j.is_null()) {
        LOG(Cfg, "%s: absent\n", path.c_str());
        return j;
    }
    constexpr size_t kChunk = 1000;
    const std::string text = j.dump();
    LOG(Cfg, "%s:\n", path.c_str());
    for (size_t start = 0; start < text.size(); start += kChunk)
        LOG(Cfg, "  %s\n", text.substr(start, kChunk).c_str());
    return j;
}

inline nlohmann::json CfgReadInstalledJsonFile(const std::string& path) {
    nlohmann::json j = CfgReadJsonFile(path);
    if (j.is_null()) CfgFatal(path, "is missing: the installation is damaged");
    return j;
}

inline const nlohmann::json& CfgRequireArray(const nlohmann::json& root,
                                             const char* key,
                                             const std::string& path) {
    if (!root.contains(key) || !root[key].is_array())
        CfgFatal(path, std::string("'") + key + "' must be an array");
    return root[key];
}

inline std::string CfgRequireString(const nlohmann::json& obj, const char* key,
                                    const std::string& path,
                                    const std::string& ctx) {
    if (!obj.contains(key) || !obj[key].is_string() ||
        obj[key].get<std::string>().empty())
        CfgFatal(path, ctx + "." + key + " must be a non-empty string");
    return obj[key].get<std::string>();
}

inline std::string CfgReadOptString(const nlohmann::json& obj, const char* key,
                                    const std::string& path,
                                    const std::string& ctx) {
    if (!obj.contains(key)) return {};
    const auto& v = obj[key];
    if (v.is_null()) return {};
    if (!v.is_string())
        CfgFatal(path, ctx + "." + key + " must be a string (or null)");
    return v.get<std::string>();
}

inline int CfgReadOptInt(const nlohmann::json& obj, const char* key,
                         const std::string& path, const std::string& ctx) {
    if (!obj.contains(key)) return 0;
    const auto& v = obj[key];
    if (v.is_null()) return 0;
    if (!v.is_number_integer())
        CfgFatal(path, ctx + "." + key + " must be an integer");
    return v.get<int>();
}

inline bool CfgReadOptBool(const nlohmann::json& obj, const char* key,
                           const std::string& path, const std::string& ctx) {
    if (!obj.contains(key)) return false;
    const auto& v = obj[key];
    if (v.is_null()) return false;
    if (!v.is_boolean())
        CfgFatal(path, ctx + "." + key + " must be a boolean");
    return v.get<bool>();
}
