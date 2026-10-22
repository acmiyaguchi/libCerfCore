#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 10.8 "HW_CLKCTRL memory map": one 32-bit register per 0x10
   bytes from 0x8004_0000 to 0x8004_0200, none between 0x170 and 0x1AF. */
struct Imx28ClkctrlReg {
    bool     present;
    bool     sct;
    uint32_t reset;
    uint32_t writable;
    uint32_t derived;
    uint32_t gated_div = 0;
};

constexpr uint32_t kImx28ClkctrlRegCount = 0x21u;

constexpr uint32_t kClkPll0Ctrl0 = 0x00u >> 4;
constexpr uint32_t kClkPll0Ctrl1 = 0x10u >> 4;
constexpr uint32_t kClkPll1Ctrl0 = 0x20u >> 4;
constexpr uint32_t kClkPll1Ctrl1 = 0x30u >> 4;
constexpr uint32_t kClkCpu       = 0x50u >> 4;
constexpr uint32_t kClkEnet      = 0x140u >> 4;
constexpr uint32_t kClkHbus     = 0x60u >> 4;
constexpr uint32_t kClkXbus     = 0x70u >> 4;
constexpr uint32_t kClkSaif0    = 0x100u >> 4;
constexpr uint32_t kClkSaif1    = 0x110u >> 4;
constexpr uint32_t kClkFlexcan   = 0x160u >> 4;
constexpr uint32_t kClkFrac0     = 0x1B0u >> 4;
constexpr uint32_t kClkFrac1     = 0x1C0u >> 4;
constexpr uint32_t kClkClkseq    = 0x1D0u >> 4;
constexpr uint32_t kClkReset     = 0x1E0u >> 4;

/* Section 10.8 memory map "Reset value"; 10.8.9 XTAL RSRVD3, 10.8.26 CLKSEQ RSRVD0-2,
   10.8.21 ENET TIME_SEL / DIV_TIME, 10.8.27 RESET.CHIP Reset rows. */
constexpr Imx28ClkctrlReg kImx28ClkctrlRegs[kImx28ClkctrlRegCount] = {
    /* 0x000 PLL0CTRL0  10.8.1  LFR_SEL 29:28 CP_SEL 25:24 DIV_SEL 21:20 EN_USB_CLKS 18 POWER 17 */
    {true,  true,  0x00000000u, 0x33360000u, 0},
    /* 0x010 PLL0CTRL1  10.8.2  FORCE_LOCK 30 (rw); LOCK 31, LOCK_COUNT 15:0 (ro) */
    {true,  false, 0x00000000u, 0x40000000u, 0x8000FFFFu},
    /* 0x020 PLL1CTRL0  10.8.3  as PLL0CTRL0 plus CLKGATEEMI 31 */
    {true,  true,  0x80000000u, 0xB3360000u, 0},
    /* 0x030 PLL1CTRL1  10.8.4 */
    {true,  false, 0x00000000u, 0x40000000u, 0x8000FFFFu},
    /* 0x040 PLL2CTRL0  10.8.5  CLKGATE 31 LFR_SEL 29:28 HOLD_RING_OFF_B 26 CP_SEL 25:24 POWER 23 */
    {true,  true,  0x80000000u, 0xB7800000u, 0},
    /* 0x050 CPU        10.8.6  DIV_XTAL_FRAC_EN 26 DIV_XTAL 25:16 INTERRUPT_WAIT 12
                                DIV_CPU 5:0; BUSY_REF_XTAL/CPU 29:28 ro */
    {true,  true,  0x00010001u, 0x07FF103Fu, 0},
    /* 0x060 HBUS       10.8.7  bits 30:29, 27:16, DIV_FRAC_EN 5, DIV 4:0; ASM_BUSY 31 ro */
    {true,  true,  0x00000001u, 0x6FFF003Fu, 0},
    /* 0x070 XBUS       10.8.8  AUTO_CLEAR_DIV_ENABLE 11 DIV_FRAC_EN 10 DIV 9:0; BUSY 31 ro */
    {true,  false, 0x00000100u, 0x00000FFFu, 0},
    /* 0x080 XTAL       10.8.9  UART_CLK_GATE 31 PWM_CLK24M_GATE 29 TIMROT_CLK32K_GATE 26 */
    {true,  true,  0x60000001u, 0xA4000000u, 0},
    /* 0x090 SSP0       10.8.10 CLKGATE 31, DIV_FRAC_EN 9, DIV 8:0; BUSY 29 ro */
    {true,  false, 0x80000001u, 0x800003FFu, 0, 0x1FFu},
    /* 0x0A0 SSP1       10.8.11 */
    {true,  false, 0x80000001u, 0x800003FFu, 0, 0x1FFu},
    /* 0x0B0 SSP2       10.8.12 */
    {true,  false, 0x80000001u, 0x800003FFu, 0, 0x1FFu},
    /* 0x0C0 SSP3       10.8.13 */
    {true,  false, 0x80000001u, 0x800003FFu, 0, 0x1FFu},
    /* 0x0D0 GPMI       10.8.14 CLKGATE 31, DIV_FRAC_EN 10, DIV 9:0 */
    {true,  false, 0x80000001u, 0x800007FFu, 0, 0x3FFu},
    /* 0x0E0 SPDIF      10.8.15 CLKGATE 31 */
    {true,  false, 0x80000000u, 0x80000000u, 0},
    /* 0x0F0 EMI        10.8.16 CLKGATE 31 SYNC_MODE_EN 30 DIV_XTAL 11:8 DIV_EMI 5:0; BUSY 29:26 ro */
    {true,  false, 0x80000101u, 0xC0000F3Fu, 0},
    /* 0x100 SAIF0      10.8.17 CLKGATE 31, DIV_FRAC_EN 16, DIV 15:0; BUSY 29 ro */
    {true,  false, 0x80000001u, 0x8001FFFFu, 0, 0xFFFFu},
    /* 0x110 SAIF1      10.8.18 */
    {true,  false, 0x80000001u, 0x8001FFFFu, 0, 0xFFFFu},
    /* 0x120 DIS_LCDIF  10.8.19 CLKGATE 31, DIV_FRAC_EN 13, DIV 12:0 */
    {true,  false, 0x80000001u, 0x80003FFFu, 0, 0x1FFFu},
    /* 0x130 ETM        10.8.20 CLKGATE 31, DIV_FRAC_EN 7, DIV 6:0 */
    {true,  false, 0x80000001u, 0x800000FFu, 0, 0x7Fu},
    /* 0x140 ENET       10.8.21 SLEEP 31 DISABLE 30 DIV_TIME 26:21 TIME_SEL 20:19 CLK_OUT_EN 18
                                RESET_BY_SW_CHIP 17 RESET_BY_SW 16; STATUS 29 (derived), BUSY_TIME 27 ro */
    {true,  false, 0xE0100000u, 0xC7FF0000u, 0x20000000u},
    /* 0x150 HSADC      10.8.22 RESETB 30 FREQDIV 29:28 */
    {true,  false, 0x00000000u, 0x70000000u, 0},
    /* 0x160 FLEXCAN    10.8.23 STOP_CAN0 30 STOP_CAN1 28; CAN0_STATUS 29 CAN1_STATUS 27 (derived) */
    {true,  false, 0x78000000u, 0x50000000u, 0x28000000u},
    {false, false, 0, 0, 0},   /* 0x170 */
    {false, false, 0, 0, 0},   /* 0x180 */
    {false, false, 0, 0, 0},   /* 0x190 */
    {false, false, 0, 0, 0},   /* 0x1A0 */
    /* 0x1B0 FRAC0      10.8.24 gates 31/23/15/7, FRAC 29:24 21:16 13:8 5:0; STABLE 30/22/14/6 ro */
    {true,  true,  0x92929292u, 0xBFBFBFBFu, 0},
    /* 0x1C0 FRAC1      10.8.25 gates 23/15/7, FRAC 21:16 13:8 5:0; STABLE 22/14/6 ro */
    {true,  true,  0x00929292u, 0x00BFBFBFu, 0},
    /* 0x1D0 CLKSEQ     10.8.26 BYPASS_CPU 18, BYPASS_DIS_LCDIF 14, BYPASS_* 8:0 */
    {true,  true,  0x004483FFu, 0x000441FFu, 0},
    /* 0x1E0 RESET      10.8.27 WDOG_POR_DISABLE 5 EXTERNAL_RESET_ENABLE 4 THERMAL_RESET_ENABLE 3
                                CHIP 1 DIG 0 */
    {true,  false, 0x00000012u, 0x0000003Bu, 0},
    /* 0x1F0 STATUS     10.8.28 read-only */
    {true,  false, 0x00000000u, 0x00000000u, 0},
    /* 0x200 VERSION    10.8.29 read-only */
    {true,  false, 0x0C020000u, 0x00000000u, 0},
};

}
