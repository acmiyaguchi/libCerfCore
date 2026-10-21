#include <cerfcore/core/device_config_refresh.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/config_json.h>
#include "config_loader.h"
#include "config_mutable_fields.h"
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>

namespace cerfcore {

REGISTER_SERVICE(DeviceConfigRefresh);

void DeviceConfigRefresh::RegisterListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void DeviceConfigRefresh::Refresh() {
    auto& config = emu_.Get<DeviceConfig>();
    CfgResetMutableFields(config);

    const std::string dir = GetDeviceDir(config.device_name);
    static const char* const kFiles[] = { "cerf.json", "cerf-user.json" };
    for (const char* name : kFiles) {
        const std::string path = dir + name;
        nlohmann::json j = CfgReadLoggedJsonFile(path);
        if (j.is_null()) continue;
        CfgLoadMutableFields(j, config, path);
    }
    emu_.Get<ConfigLoader>().ApplyBoardPanelDefault(config);

    LOG(Cfg, "DeviceConfigRefresh: %ux%u dpi=%u bpp=%u font=%d(set=%d) cleartype=%d(set=%d) untrusted=%d scheme='%s' share='%s' mount='%s'\n",
        config.board_configurable_screen_width,
        config.board_configurable_screen_height, config.screen_dpi,
        config.board_configurable_screen_bpp,
        config.guest_additions_font_size,
        config.guest_additions_font_size_set ? 1 : 0,
        config.guest_additions_cleartype ? 1 : 0,
        config.guest_additions_cleartype_set ? 1 : 0,
        config.guest_additions_allow_untrusted_apps ? 1 : 0,
        config.guest_additions_color_scheme.c_str(),
        config.share_folder.c_str(),
        config.share_folder_mount_point.c_str());

    for (auto& fn : listeners_) fn();
}

}
