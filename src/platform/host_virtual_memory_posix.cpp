#include "host_virtual_memory.h"

#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

#include <cerfcore/core/log.h>

namespace cerfcore {

namespace host_platform {

namespace {

size_t PageBytes() { return static_cast<size_t>(::sysconf(_SC_PAGESIZE)); }

}

uint8_t* AllocateZeroed(size_t size) {
    const size_t header = PageBytes();
    void* base = ::mmap(nullptr, header + size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        LOG(Caution, "mmap of 0x%zX bytes failed (errno=%d)\n", size, errno);
        return nullptr;
    }
    *static_cast<size_t*>(base) = header + size;
    return static_cast<uint8_t*>(base) + header;
}

void Release(uint8_t* memory) {
    uint8_t* const base = memory - PageBytes();
    ::munmap(base, *reinterpret_cast<const size_t*>(base));
}

}

}
