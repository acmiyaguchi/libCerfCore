#pragma once

#include <cstdint>

namespace cerfcore {

struct Vr41xxBcuBootWrite {
    uint32_t offset = 0;
    uint16_t value  = 0;
};

}
