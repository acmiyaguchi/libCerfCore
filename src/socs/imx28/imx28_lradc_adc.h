#pragma once

#include <cstdint>

namespace cerfcore {

class CerfEmulator;

inline constexpr uint32_t kImx28LradcBase = 0x80050000u;

/* MCIMX28RM Rev. 2 sections 38.1 and 38.2: the 12 bit successive approximation converter. */
class Imx28LradcAdc {
public:
    explicit Imx28LradcAdc(CerfEmulator& emu) : emu_(emu) {}

    uint32_t Convert(uint32_t channel, uint32_t select, uint32_t ctrl0, uint32_t ctrl2,
                     uint32_t conversion);

private:
    CerfEmulator& emu_;
};

}
