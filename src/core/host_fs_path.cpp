#include <cerfcore/core/host_fs_path.h>

#include <cerfcore/core/utf16.h>

std::filesystem::path HostFsPath(const std::string& utf8) {
    return std::filesystem::path(Utf8ToUtf16(utf8));
}
