#pragma once

#include <string>

namespace cerfcore {

std::string GetCerfDir();
std::string DbJsonPath();
std::string GetDeviceDir(const std::string& device_name);
std::string CeAppPath(const std::string& arch_dir, const std::string& file);
std::string UserConfigPath(const std::string& device_name);
bool        IsAbsoluteHostPath(const std::string& path);
std::string ResolveDeviceFile(const std::string& device_name, const std::string& filename);

}
