#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cerfcore {

namespace host_platform {

struct FileMapping {
    void*    file        = nullptr;
    void*    section     = nullptr;
    uint64_t size        = 0;
    uint32_t granularity = 0;
};

bool     OpenMappedFile(const std::string& path, FileMapping& mapping);
void     CloseMappedFile(FileMapping& mapping);
uint8_t* MapFileView(const FileMapping& mapping, uint64_t offset, size_t length);
void     UnmapFileView(uint8_t* view, size_t length);

}

}
