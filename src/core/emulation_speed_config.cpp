#include "emulation_speed_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <numeric>

namespace cerfcore {

REGISTER_SERVICE(EmulationSpeedConfig);

namespace {

constexpr const char* kThrottleKey = "throttle_multiplier";
constexpr const char* kCpuRateKey  = "cpu_rate_multiplier";
constexpr uint64_t    kThousandths = 1000u;
constexpr double      kMaxThousandths = 9.0e18;

EmulationSpeedConfig::Multiplier ReadMultiplier(const nlohmann::json& root, const char* key,
                                                bool zero_allowed, const std::string& path,
                                                EmulationSpeedConfig::Multiplier current) {
    if (!root.contains(key)) return current;
    const auto& v = root[key];
    if (!v.is_number()) CfgFatal(path, std::string("'") + key + "' must be a number");
    const double scaled = v.get<double>() * static_cast<double>(kThousandths);
    if (!std::isfinite(scaled) || scaled < 0.0 || scaled >= kMaxThousandths)
        CfgFatal(path, std::string("'") + key + "' is out of range");
    const uint64_t thousandths = static_cast<uint64_t>(std::llround(scaled));
    if (thousandths == 0u && !zero_allowed)
        CfgFatal(path, std::string("'") + key + "' must be greater than 0");
    const uint64_t g = std::gcd(thousandths, kThousandths);
    return EmulationSpeedConfig::Multiplier{thousandths / g, kThousandths / g};
}

}

void EmulationSpeedConfig::OnReady() {
    const std::string& device = emu_.Get<DeviceConfig>().device_name;
    Load(GetDeviceDir(device) + "cerf.json");
    Load(UserConfigPath(device));
    LOG(Cfg, "throttle multiplier %llu/%llu, CPU rate multiplier %llu/%llu\n",
        static_cast<unsigned long long>(throttle_.num),
        static_cast<unsigned long long>(throttle_.den),
        static_cast<unsigned long long>(cpu_rate_.num),
        static_cast<unsigned long long>(cpu_rate_.den));
}

void EmulationSpeedConfig::Load(const std::string& path) {
    const nlohmann::json root = CfgReadJsonFile(path);
    if (root.is_null()) return;
    throttle_ = ReadMultiplier(root, kThrottleKey, true, path, throttle_);
    cpu_rate_ = ReadMultiplier(root, kCpuRateKey, false, path, cpu_rate_);
}

}
