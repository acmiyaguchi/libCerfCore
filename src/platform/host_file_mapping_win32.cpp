#include "host_file_mapping.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cerfcore/platform/wide_text_win32.h>

namespace cerfcore {

namespace host_platform {

bool OpenMappedFile(const std::string& path, FileMapping& mapping) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    mapping.granularity = si.dwAllocationGranularity;

    HANDLE file = CreateFileW(Utf8ToWide(path.c_str()).c_str(), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    mapping.file = file;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(file, &sz) || sz.QuadPart <= 0) return false;
    mapping.size = static_cast<uint64_t>(sz.QuadPart);

    mapping.section = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    return mapping.section != nullptr;
}

void CloseMappedFile(FileMapping& mapping) {
    if (mapping.section) CloseHandle(mapping.section);
    if (mapping.file) CloseHandle(mapping.file);
    mapping.section = nullptr;
    mapping.file    = nullptr;
}

uint8_t* MapFileView(const FileMapping& mapping, uint64_t offset, size_t length) {
    return static_cast<uint8_t*>(MapViewOfFile(mapping.section, FILE_MAP_READ,
                                               static_cast<DWORD>(offset >> 32),
                                               static_cast<DWORD>(offset & 0xFFFFFFFFu),
                                               length));
}

void UnmapFileView(uint8_t* view, size_t) {
    UnmapViewOfFile(view);
}

}

}
