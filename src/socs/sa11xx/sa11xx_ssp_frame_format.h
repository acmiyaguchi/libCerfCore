#pragma once

#include <cstdint>

struct Sa11xxSspFrameFormat {
    uint64_t period    = 2;
    uint64_t push_cont = 4;
    uint64_t push_last = 4;
    uint32_t data_bits = 1;
    uint32_t tx_bits   = 1;
    uint32_t tx_shift  = 15;

    static Sa11xxSspFrameFormat Of(uint32_t sscr0, uint32_t sscr1);
};
