#include "host_file_mapping.h"

#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "host_fd_handle.h"

namespace cerfcore {

namespace host_platform {

bool OpenMappedFile(const std::string& path, FileMapping& mapping) {
    mapping.granularity = static_cast<uint32_t>(::sysconf(_SC_PAGESIZE));
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    mapping.file = HandleOf(fd);
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) return false;
    mapping.size    = static_cast<uint64_t>(st.st_size);
    mapping.section = mapping.file;
    return true;
}

void CloseMappedFile(FileMapping& mapping) {
    if (mapping.file) ::close(FdOf(mapping.file));
    mapping.section = nullptr;
    mapping.file    = nullptr;
}

uint8_t* MapFileView(const FileMapping& mapping, uint64_t offset, size_t length) {
    void* view = ::mmap(nullptr, length, PROT_READ, MAP_SHARED, FdOf(mapping.section),
                        static_cast<off_t>(offset));
    return view == MAP_FAILED ? nullptr : static_cast<uint8_t*>(view);
}

void UnmapFileView(uint8_t* view, size_t length) { ::munmap(view, length); }

}

}
