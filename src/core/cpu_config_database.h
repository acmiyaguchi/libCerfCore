#pragma once

#include <cerfcore/core/board_database.h>
#include <cerfcore/core/service.h>

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

enum class CpuConfigParamType { Boolean, Integer, Hex, Enum };

struct CpuConfigParam {
    std::string              id;
    CpuArch                  arch;
    CpuConfigParamType       type;
    std::vector<std::string> enum_values;
};

struct CpuConfigValue {
    bool        boolean = false;
    uint32_t    number  = 0;
    std::string enum_id;
};

using CpuConfigValues = std::map<std::string, CpuConfigValue, std::less<>>;

class CpuConfigDatabase : public Service {
public:
    using Service::Service;

    void OnReady() override;

    const CpuConfigParam* FindParam(std::string_view id) const;

    CpuConfigValues ResolvePreset(std::string_view soc_id) const;

    CpuConfigValue ParseValue(const CpuConfigParam& param, const nlohmann::json& value,
                              const std::string& path, const std::string& ctx) const;

private:
    struct Preset {
        std::string     id;
        std::string     soc_id;
        std::string     inherits;
        CpuConfigValues values;
    };

    const Preset& PresetById(std::string_view id) const;
    void          Apply(const Preset& preset, CpuConfigValues& out) const;

    std::vector<CpuConfigParam> params_;
    std::vector<Preset>         presets_;
};

}
