#include "cpu_config.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include "no_emulation_runtime_service.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(CpuConfig);

namespace {

constexpr const char* kUserConfigKey = "cpu_config";

}

void CpuConfig::OnReady() {
    emu_.Get<NoEmulationRuntimeService>().EnsureEmulationPrevented();
    const auto soc_id = emu_.Get<BoardContext>().GetSocId();
    values_ = emu_.Get<CpuConfigDatabase>().ResolvePreset(soc_id);
    const std::string db_path = DbJsonPath();
    for (const auto& [id, value] : values_) origins_[id] = db_path;
    ApplyUserOverrides();
}

void CpuConfig::ApplyUserOverrides() {
    const std::string path = UserConfigPath(emu_.Get<DeviceConfig>().device_name);
    const nlohmann::json root = CfgReadJsonFile(path);
    if (root.is_null() || !root.contains(kUserConfigKey)) return;
    const auto& overrides = root[kUserConfigKey];
    if (!overrides.is_object())
        CfgFatal(path, std::string("'") + kUserConfigKey + "' must be an object");

    const auto& db   = emu_.Get<CpuConfigDatabase>();
    const auto  arch = emu_.Get<BoardContext>().GetCpuArch();
    for (const auto& [id, value] : overrides.items()) {
        const CpuConfigParam* param = db.FindParam(id);
        if (!param || param->arch != arch) {
            LOG(Cfg, "cpu_config.%s does not apply to this board\n", id.c_str());
            continue;
        }
        values_[id]  = db.ParseValue(*param, value, path, kUserConfigKey);
        origins_[id] = path;
        LOG(Cfg, "cpu_config.%s overridden by %s\n", id.c_str(), path.c_str());
    }
}

bool CpuConfig::Boolean(std::string_view id) const {
    return Value(id, {CpuConfigParamType::Boolean}).boolean;
}

uint32_t CpuConfig::Number(std::string_view id) const {
    return Value(id, {CpuConfigParamType::Integer, CpuConfigParamType::Hex}).number;
}

const std::string& CpuConfig::EnumId(std::string_view id) const {
    return Value(id, {CpuConfigParamType::Enum}).enum_id;
}

const std::string& CpuConfig::Origin(std::string_view id) const {
    const auto it = origins_.find(id);
    if (it == origins_.end()) MissingValue(id);
    return it->second;
}

const CpuConfigValue& CpuConfig::Value(
    std::string_view id, std::initializer_list<CpuConfigParamType> types) const {
    const CpuConfigParam* param = emu_.Get<CpuConfigDatabase>().FindParam(id);
    if (!param)
        CfgFatal(DbJsonPath(), "cpu_config_params[] has no '" + std::string(id) + "'");
    if (std::find(types.begin(), types.end(), param->type) == types.end())
        CfgFatal(DbJsonPath(), "cpu_config_params[" + param->id +
                               "].type does not match how CERF reads it");
    const auto it = values_.find(id);
    if (it == values_.end()) MissingValue(id);
    return it->second;
}

void CpuConfig::MissingValue(std::string_view id) const {
    CfgFatal(DbJsonPath(), "cpu_configs[] for soc '" +
                           std::string(emu_.Get<BoardContext>().GetSocId()) +
                           "' assigns no '" + std::string(id) + "'");
}

void CpuConfig::UnmappedEnum(std::string_view id, const std::string& value) const {
    CfgFatal(Origin(id), "cpu_config." + std::string(id) + " '" + value +
                         "' is unknown to this build");
}

}
