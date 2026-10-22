#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 11.12 memory map: register index is offset >> 4. */
enum Imx28PowerIndex : uint32_t {
    kPwrCtrl, kPwr5vCtrl, kPwrMinpwr, kPwrCharge, kPwrVdddCtrl, kPwrVddaCtrl, kPwrVddioCtrl,
    kPwrVddmemCtrl, kPwrDcdc4p2, kPwrMisc, kPwrDclimits, kPwrLoopCtrl, kPwrSts, kPwrSpeed,
    kPwrBattMonitor, kPwrGap0F, kPwrReset, kPwrDebug, kPwrThermal, kPwrUsb1Ctrl, kPwrSpecial,
    kPwrVersion, kPwrAnaClkCtrl, kPwrRefCtrl, kPwrRegCount
};

struct Imx28PowerReg {
    uint32_t reset;
    uint32_t writable;
};

/* Section 11.12 memory map reset values; W-row fields of figures 11.12.1-11.12.23; STS and
   VERSION are R with no SCT alias. */
constexpr Imx28PowerReg kImx28PowerRegs[kPwrRegCount] = {
    /* CTRL         */ {0x00010024u, 0x09FFFFFFu},
    /* 5VCTRL       */ {0x20100490u, 0x3713F7FFu},
    /* MINPWR       */ {0x00000000u, 0x00007FFFu},
    /* CHARGE       */ {0x00010000u, 0x075D3F3Fu},
    /* VDDDCTRL     */ {0x00320710u, 0xF0F3071Fu},
    /* VDDACTRL     */ {0x0000270Cu, 0x000F371Fu},
    /* VDDIOCTRL    */ {0x00002406u, 0x00F7371Fu},
    /* VDDMEMCTRL   */ {0x00000230u, 0x000007FFu},
    /* DCDC4P2      */ {0x00000018u, 0xF3F71F1Fu},
    /* MISC         */ {0x00000000u, 0x0000007Fu},
    /* DCLIMITS     */ {0x00000C5Fu, 0x00007F7Fu},
    /* LOOPCTRL     */ {0x00000021u, 0x001FF7F3u},
    /* STS          */ {0x00000200u, 0x00000000u},
    /* SPEED        */ {0x00000000u, 0x000000C3u},
    /* BATTMONITOR  */ {0x00000A00u, 0x03FF0F1Fu},
    /* gap 0xF0     */ {0x00000000u, 0x00000000u},
    /* RESET        */ {0x00000000u, 0xFFFF0007u},
    /* DEBUG        */ {0x00000000u, 0x0000000Fu},
    /* THERMAL      */ {0x00000080u, 0x000001FFu},
    /* USB1CTRL     */ {0x0000000Eu, 0x0000000Fu},
    /* SPECIAL      */ {0x00000000u, 0xFFFFFFFFu},
    /* VERSION      */ {0x04000000u, 0x00000000u},
    /* ANACLKCTRL   */ {0x84000400u, 0xFC000737u},
    /* REFCTRL      */ {0x00000000u, 0x077B7FF0u},
};

}
