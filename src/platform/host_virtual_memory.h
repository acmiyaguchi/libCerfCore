#pragma once

#include <cstddef>
#include <cstdint>

namespace cerfcore {

namespace host_platform {

uint8_t* AllocateZeroed(size_t size);
void     Release(uint8_t* memory);

}

}
