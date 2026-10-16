#include <cerfcore/core/folder_share_config.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config_refresh.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/platform/host_file.h>

REGISTER_SERVICE(FolderShareConfig);

void FolderShareConfig::OnReady() {
    ApplyFromConfig();
    emu_.Get<DeviceConfigRefresh>().RegisterListener(
        [this] { ApplyFromConfig(); });
}

void FolderShareConfig::ApplyFromConfig() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    const std::string& name = cfg.share_folder_mount_point.empty()
        ? emu_.Get<BoardDatabase>().GaSharedFolderMountPoint()
        : cfg.share_folder_mount_point;
    std::u16string mount = Utf8ToUtf16(name);

    const std::string& p = cfg.share_folder;
    if (p.empty()) {
        Set(false, std::string(), std::move(mount));
        return;
    }

    std::string root = IsAbsoluteHostPath(p) ? p : GetCerfDir() + p;
    LOG(GuestAdditions, "[FolderShare] host root '%s' exists=%d mount '%s'\n",
        root.c_str(), (int)host_platform::HostPathExists(root), name.c_str());

    Set(true, std::move(root), std::move(mount));
}
