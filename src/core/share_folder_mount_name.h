#pragma once

#include <cstddef>
#include <string>

namespace cerfcore {

constexpr size_t kShareFolderMountNameMaxWchars = 63;

bool IsValidShareFolderMountName(const std::string& utf8);

}
