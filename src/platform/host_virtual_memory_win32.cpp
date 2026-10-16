#include "host_virtual_memory.h"

#define NOMINMAX
#include <windows.h>

#include <cerfcore/core/log.h>

namespace cerfcore {

namespace host_platform {

uint8_t* AllocateZeroed(size_t size) {
    void* memory = ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory) {
        LOG(Caution, "VirtualAlloc of 0x%zX bytes failed (GLE=%lu)\n", size, ::GetLastError());
    }
    return static_cast<uint8_t*>(memory);
}

void Release(uint8_t* memory) {
    ::VirtualFree(memory, 0, MEM_RELEASE);
}

}

}
