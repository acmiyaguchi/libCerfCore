#pragma once

#include <cstdint>

constexpr bool Msm8255ValueSetContains(const uint32_t* values, uint32_t count,
                                       uint32_t value) {
    for (uint32_t i = 0; i < count; ++i) {
        if (values[i] == value) return true;
    }
    return false;
}
