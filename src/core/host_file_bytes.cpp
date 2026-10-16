#include "host_file_bytes.h"

#include <cerfcore/core/host_fs_path.h>
#include <cerfcore/platform/host_file.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace cerfcore {

bool TryReadHostFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(HostFsPath(path), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto sz = f.tellg();
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return true;
}

std::vector<uint8_t> ReadHostFileBytes(const std::string& path) {
    std::vector<uint8_t> bytes;
    TryReadHostFileBytes(path, bytes);
    return bytes;
}

HostWriteResult WriteHostFileBytes(const std::string& path, const uint8_t* data, size_t size) {
    host_platform::HostFile file;
    if (!file.Open(path, host_platform::FileMode::kWriteNew)) return HostWriteResult::kOpenFailed;
    constexpr size_t kChunk = 0x40000000u;
    while (size != 0) {
        const uint32_t want = static_cast<uint32_t>(std::min(size, kChunk));
        uint32_t done = 0;
        if (!file.Write(data, want, done) || done != want) return HostWriteResult::kShortWrite;
        data += done;
        size -= done;
    }
    return HostWriteResult::kOk;
}

bool HostFileNonEmpty(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path p = HostFsPath(path);
    if (!std::filesystem::is_regular_file(p, ec) || ec) return false;
    const auto size = std::filesystem::file_size(p, ec);
    return !ec && size != 0;
}

}
