#include "cpu_config_database.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

REGISTER_SERVICE(CpuConfigDatabase);

namespace {

using nlohmann::json;

CpuConfigParamType ParseParamType(const std::string& value, const std::string& path,
                                  const std::string& ctx) {
    if (value == "boolean") return CpuConfigParamType::Boolean;
    if (value == "integer") return CpuConfigParamType::Integer;
    if (value == "hex")     return CpuConfigParamType::Hex;
    if (value == "enum")    return CpuConfigParamType::Enum;
    CfgFatal(path, ctx + ".type '" + value + "' is unknown");
}

bool ParseHex(const std::string& text, uint32_t& out) {
    if (text.size() < 3 || text.size() > 10 || text[0] != '0' ||
        (text[1] != 'x' && text[1] != 'X'))
        return false;
    uint32_t value = 0;
    for (size_t i = 2; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isxdigit(c)) return false;
        const uint32_t digit = std::isdigit(c) ? c - '0' : (std::tolower(c) - 'a' + 10);
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}

}

void CpuConfigDatabase::OnReady() {
    const std::string path = DbJsonPath();
    const json root = CfgReadInstalledJsonFile(path);

    for (const auto& p : CfgRequireArray(root, "cpu_config_params", path)) {
        if (!p.is_object()) CfgFatal(path, "cpu_config_params[] entries must be objects");
        CpuConfigParam param;
        param.id = CfgRequireString(p, "id", path, "cpu_config_params[]");
        const std::string ctx = "cpu_config_params[" + param.id + "]";
        param.arch = ParseCpuArch(CfgRequireString(p, "arch", path, ctx), path, ctx);
        param.type = ParseParamType(CfgRequireString(p, "type", path, ctx), path, ctx);
        if (param.type == CpuConfigParamType::Enum) {
            for (const auto& v : CfgRequireArray(p, "enum_values", path)) {
                if (!v.is_object()) CfgFatal(path, ctx + ".enum_values[] entries must be objects");
                param.enum_values.push_back(
                    CfgRequireString(v, "id", path, ctx + ".enum_values[]"));
            }
        }
        params_.push_back(std::move(param));
    }

    for (const auto& c : CfgRequireArray(root, "cpu_configs", path)) {
        if (!c.is_object()) CfgFatal(path, "cpu_configs[] entries must be objects");
        Preset preset;
        preset.id       = CfgReadOptString(c, "id", path, "cpu_configs[]");
        preset.soc_id   = CfgReadOptString(c, "soc_id", path, "cpu_configs[]");
        preset.inherits = CfgReadOptString(c, "inherits_cpu_config_id", path, "cpu_configs[]");
        const std::string ctx =
            "cpu_configs[" + (preset.id.empty() ? preset.soc_id : preset.id) + "]";
        if (!c.contains("values") || !c["values"].is_object())
            CfgFatal(path, ctx + ".values must be an object");
        for (const auto& [key, value] : c["values"].items()) {
            const CpuConfigParam* param = FindParam(key);
            if (!param) CfgFatal(path, ctx + ".values." + key + " is not in cpu_config_params[]");
            preset.values[key] = ParseValue(*param, value, path, ctx + ".values");
        }
        presets_.push_back(std::move(preset));
    }
}

const CpuConfigParam* CpuConfigDatabase::FindParam(std::string_view id) const {
    for (const auto& p : params_)
        if (p.id == id) return &p;
    return nullptr;
}

CpuConfigValues CpuConfigDatabase::ResolvePreset(std::string_view soc_id) const {
    for (const auto& preset : presets_) {
        if (preset.soc_id != soc_id) continue;
        CpuConfigValues out;
        Apply(preset, out);
        return out;
    }
    CfgFatal(DbJsonPath(), "soc '" + std::string(soc_id) + "' has no cpu_configs[] row");
}

void CpuConfigDatabase::Apply(const Preset& preset, CpuConfigValues& out) const {
    if (!preset.inherits.empty()) Apply(PresetById(preset.inherits), out);
    for (const auto& [key, value] : preset.values) out[key] = value;
}

const CpuConfigDatabase::Preset& CpuConfigDatabase::PresetById(std::string_view id) const {
    for (const auto& preset : presets_)
        if (preset.id == id) return preset;
    CfgFatal(DbJsonPath(), "cpu_configs[] has no row with id '" + std::string(id) + "'");
}

CpuConfigValue CpuConfigDatabase::ParseValue(const CpuConfigParam& param, const json& value,
                                             const std::string& path,
                                             const std::string& ctx) const {
    const std::string where = ctx + "." + param.id;
    CpuConfigValue out;
    switch (param.type) {
    case CpuConfigParamType::Boolean:
        if (!value.is_boolean()) CfgFatal(path, where + " must be a boolean");
        out.boolean = value.get<bool>();
        return out;
    case CpuConfigParamType::Integer:
        if (!value.is_number_unsigned() || value.get<uint64_t>() > UINT32_MAX)
            CfgFatal(path, where + " must be an unsigned 32-bit integer");
        out.number = value.get<uint32_t>();
        return out;
    case CpuConfigParamType::Hex:
        if (!value.is_string() || !ParseHex(value.get<std::string>(), out.number))
            CfgFatal(path, where + " must be a 32-bit hex string such as \"0x1F\"");
        return out;
    case CpuConfigParamType::Enum:
        if (!value.is_string()) CfgFatal(path, where + " must be a string");
        out.enum_id = value.get<std::string>();
        if (std::find(param.enum_values.begin(), param.enum_values.end(), out.enum_id) ==
            param.enum_values.end())
            CfgFatal(path, where + " '" + out.enum_id + "' is unknown");
        return out;
    }
    CfgFatal(path, where + " has a type this build does not parse");
}
