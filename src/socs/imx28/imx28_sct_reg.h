#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 39.3 (Register Macro Usage, Background) "The SCT registers always
   read back 0"; section 10.8.1: HW_CLKCTRL_PLL0CTRL0 0x000, _SET 0x004, _CLR 0x008, _TOG 0x00C. */
enum class Imx28SctOp : uint32_t { Write = 0u, Set = 1u, Clr = 2u, Tog = 3u };

struct Imx28SctAccess {
    uint32_t   reg_offset;
    Imx28SctOp op;
};

inline Imx28SctAccess Imx28DecodeSct(uint32_t offset) {
    return Imx28SctAccess{offset & ~0xFu, static_cast<Imx28SctOp>((offset >> 2) & 0x3u)};
}

inline uint32_t Imx28ApplySct(Imx28SctOp op, uint32_t current, uint32_t value) {
    switch (op) {
        case Imx28SctOp::Set: return current | value;
        case Imx28SctOp::Clr: return current & ~value;
        case Imx28SctOp::Tog: return current ^ value;
        default:              return value;
    }
}

}
