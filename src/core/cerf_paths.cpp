#include <cerfcore/core/cerf_paths.h>

#include <cerfcore/core/host_fs_path.h>
#include <cerfcore/platform/host_process.h>

#include <filesystem>

namespace cerfcore {

namespace {

constexpr char kSeparator = static_cast<char>(std::filesystem::path::preferred_separator);

}

std::string GetCerfDir() {
    const std::string app = host_platform::ApplicationPath();
    const size_t sep = app.find_last_of("\\/");
    if (sep == std::string::npos) return "";
    return app.substr(0, sep + 1);
}

std::string DbJsonPath() {
    return GetCerfDir() + "db.json";
}

std::string GetDeviceDir(const std::string& device_name) {
    return GetCerfDir() + "devices" + kSeparator + device_name + kSeparator;
}

std::string CeAppPath(const std::string& arch_dir, const std::string& file) {
    return GetCerfDir() + "ce_apps" + kSeparator + arch_dir + kSeparator + file;
}

std::string UserConfigPath(const std::string& device_name) {
    return GetDeviceDir(device_name) + "cerf-user.json";
}

bool IsAbsoluteHostPath(const std::string& path) {
    return HostFsPath(path).has_root_path();
}

std::string ResolveDeviceFile(const std::string& device_name, const std::string& filename) {
    if (IsAbsoluteHostPath(filename)) return filename;
    return GetDeviceDir(device_name) + filename;
}

}
