#pragma once

#include <filesystem>
#include <string>

namespace cerfcore {

std::filesystem::path HostFsPath(const std::string& utf8);

}
