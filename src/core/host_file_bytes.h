#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

bool TryReadHostFileBytes(const std::string& path, std::vector<uint8_t>& out);
std::vector<uint8_t> ReadHostFileBytes(const std::string& path);

enum class HostWriteResult : uint8_t { kOk, kOpenFailed, kShortWrite };

HostWriteResult WriteHostFileBytes(const std::string& path, const uint8_t* data, size_t size);

bool HostFileNonEmpty(const std::string& path);

}
