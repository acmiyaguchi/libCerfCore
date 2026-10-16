#pragma once
#include <cstdint>

namespace cerfcore {

uint64_t Imx51Gpu3dTiledAddress(uint32_t base, uint32_t pitch, uint32_t bytes,
    uint32_t x, uint32_t y);

}
