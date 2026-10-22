#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 9.4 "HW_PINCTRL memory map": reset values per register; section
   9.4.x lists _SET/_CLR/_TOG addresses for every register including DRIVE19 (9.4.35) and
   DIN0-4 (9.4.48-52), but both are "R" in the 9.4 memory map and 39.3 says "The SCT registers
   are not implemented if the primary register is read-only", so sct is false for them. Writable masks are the fields the 9.4.x field tables do not mark "Always write
   zeroes" / "Empty Description" (reserved); CTRL writes only SFTRST [31] and CLKGATE [30]
   (PRESENT [24:20] and IRQOUT [4:0] are read-only); DRIVE1 is reserved entirely. */
struct Imx28PinctrlReg {
    uint16_t offset;
    uint32_t reset;
    uint32_t writable;
    bool     sct;
};

constexpr uint32_t kImx28PinctrlRegCount = 84u;

constexpr Imx28PinctrlReg kImx28PinctrlRegs[kImx28PinctrlRegCount] = {
    {0x0000u, 0xC1F00000u, 0xC0000000u, true },  /* 9.4.1 CTRL */
    {0x0100u, 0x0000FFFFu, 0x0000FFFFu, true },  /* 9.4.2 MUXSEL0 */
    {0x0110u, 0x03FFFFFFu, 0x03FFFFFFu, true },  /* 9.4.3 MUXSEL1 */
    {0x0120u, 0xFFFFFFFFu, 0xFFFFFFFFu, true },  /* 9.4.4 MUXSEL2 */
    {0x0130u, 0xFFFFFFFFu, 0xFFFFFFFFu, true },  /* 9.4.5 MUXSEL3 */
    {0x0140u, 0xFF3FFFFFu, 0xFF3FFFFFu, true },  /* 9.4.6 MUXSEL4 */
    {0x0150u, 0x00FF0FFFu, 0x00FF0FFFu, true },  /* 9.4.7 MUXSEL5 */
    {0x0160u, 0xFFFFFFFFu, 0xFFFFFFFFu, true },  /* 9.4.8 MUXSEL6 */
    {0x0170u, 0x3FFFFF3Fu, 0x3FFFFF3Fu, true },  /* 9.4.9 MUXSEL7 */
    {0x0180u, 0xFFFFFFFFu, 0xFFFFFFFFu, true },  /* 9.4.10 MUXSEL8 */
    {0x0190u, 0x00000003u, 0x00000303u, true },  /* 9.4.11 MUXSEL9 */
    {0x01A0u, 0xFFFFFFFFu, 0xFFFFFFFFu, true },  /* 9.4.12 MUXSEL10 */
    {0x01B0u, 0x0030FFFFu, 0x0030FFFFu, true },  /* 9.4.13 MUXSEL11 */
    {0x01C0u, 0x3FFFFFFFu, 0x3FFFFFFFu, true },  /* 9.4.14 MUXSEL12 */
    {0x01D0u, 0x0003FFFFu, 0x0003FFFFu, true },  /* 9.4.15 MUXSEL13 */
    {0x0300u, 0x44444444u, 0x77777777u, true },  /* 9.4.16 DRIVE0 */
    {0x0310u, 0x00000000u, 0x00000000u, true },  /* 9.4.17 DRIVE1 */
    {0x0320u, 0x44444444u, 0x77777777u, true },  /* 9.4.18 DRIVE2 */
    {0x0330u, 0x00044444u, 0x00077777u, true },  /* 9.4.19 DRIVE3 */
    {0x0340u, 0x44444444u, 0x77777777u, true },  /* 9.4.20 DRIVE4 */
    {0x0350u, 0x44444444u, 0x77777777u, true },  /* 9.4.21 DRIVE5 */
    {0x0360u, 0x44444444u, 0x77777777u, true },  /* 9.4.22 DRIVE6 */
    {0x0370u, 0x44444444u, 0x77777777u, true },  /* 9.4.23 DRIVE7 */
    {0x0380u, 0x44444444u, 0x77777777u, true },  /* 9.4.24 DRIVE8 */
    {0x0390u, 0x44440444u, 0x77770777u, true },  /* 9.4.25 DRIVE9 */
    {0x03A0u, 0x00444444u, 0x00777777u, true },  /* 9.4.26 DRIVE10 */
    {0x03B0u, 0x00004444u, 0x00007777u, true },  /* 9.4.27 DRIVE11 */
    {0x03C0u, 0x44444444u, 0x77777777u, true },  /* 9.4.28 DRIVE12 */
    {0x03D0u, 0x44444444u, 0x77777777u, true },  /* 9.4.29 DRIVE13 */
    {0x03E0u, 0x44440444u, 0x77770777u, true },  /* 9.4.30 DRIVE14 */
    {0x03F0u, 0x04444444u, 0x07777777u, true },  /* 9.4.31 DRIVE15 */
    {0x0400u, 0x44444444u, 0x77777777u, true },  /* 9.4.32 DRIVE16 */
    {0x0410u, 0x44444444u, 0x77777777u, true },  /* 9.4.33 DRIVE17 */
    {0x0420u, 0x00040004u, 0x00070007u, true },  /* 9.4.34 DRIVE18 */
    {0x0430u, 0x00000000u, 0x00000000u, false},  /* 9.4.35 DRIVE19 */
    {0x0600u, 0x00000000u, 0x1FFF00FFu, true },  /* 9.4.36 PULL0 */
    {0x0610u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.37 PULL1 */
    {0x0620u, 0x00000000u, 0x0F3FF7FFu, true },  /* 9.4.38 PULL2 */
    {0x0630u, 0x00000000u, 0x7FF7FFFFu, true },  /* 9.4.39 PULL3 */
    {0x0640u, 0x00000000u, 0x0011FFFFu, true },  /* 9.4.40 PULL4 */
    {0x0650u, 0x00000000u, 0x04FFFFFFu, true },  /* 9.4.41 PULL5 */
    {0x0660u, 0x00000000u, 0x01FF7FFFu, true },  /* 9.4.42 PULL6 */
    {0x0700u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.43 DOUT0 */
    {0x0710u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.44 DOUT1 */
    {0x0720u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.45 DOUT2 */
    {0x0730u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.46 DOUT3 */
    {0x0740u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.47 DOUT4 */
    {0x0900u, 0x00000000u, 0x00000000u, false},  /* 9.4.48 DIN0 */
    {0x0910u, 0x00000000u, 0x00000000u, false},  /* 9.4.49 DIN1 */
    {0x0920u, 0x00000000u, 0x00000000u, false},  /* 9.4.50 DIN2 */
    {0x0930u, 0x00000000u, 0x00000000u, false},  /* 9.4.51 DIN3 */
    {0x0940u, 0x00000000u, 0x00000000u, false},  /* 9.4.52 DIN4 */
    {0x0B00u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.53 DOE0 */
    {0x0B10u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.54 DOE1 */
    {0x0B20u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.55 DOE2 */
    {0x0B30u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.56 DOE3 */
    {0x0B40u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.57 DOE4 */
    {0x1000u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.58 PIN2IRQ0 */
    {0x1010u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.59 PIN2IRQ1 */
    {0x1020u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.60 PIN2IRQ2 */
    {0x1030u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.61 PIN2IRQ3 */
    {0x1040u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.62 PIN2IRQ4 */
    {0x1100u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.63 IRQEN0 */
    {0x1110u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.64 IRQEN1 */
    {0x1120u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.65 IRQEN2 */
    {0x1130u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.66 IRQEN3 */
    {0x1140u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.67 IRQEN4 */
    {0x1200u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.68 IRQLEVEL0 */
    {0x1210u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.69 IRQLEVEL1 */
    {0x1220u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.70 IRQLEVEL2 */
    {0x1230u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.71 IRQLEVEL3 */
    {0x1240u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.72 IRQLEVEL4 */
    {0x1300u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.73 IRQPOL0 */
    {0x1310u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.74 IRQPOL1 */
    {0x1320u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.75 IRQPOL2 */
    {0x1330u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.76 IRQPOL3 */
    {0x1340u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.77 IRQPOL4 */
    {0x1400u, 0x00000000u, 0x1FFFFFFFu, true },  /* 9.4.78 IRQSTAT0 */
    {0x1410u, 0x00000000u, 0xFFFFFFFFu, true },  /* 9.4.79 IRQSTAT1 */
    {0x1420u, 0x00000000u, 0x0FFFFFFFu, true },  /* 9.4.80 IRQSTAT2 */
    {0x1430u, 0x00000000u, 0x7FFFFFFFu, true },  /* 9.4.81 IRQSTAT3 */
    {0x1440u, 0x00000000u, 0x001FFFFFu, true },  /* 9.4.82 IRQSTAT4 */
    {0x1A40u, 0x08888888u, 0x0FFFFFFFu, true },  /* 9.4.83 EMI_ODT_CTRL */
    {0x1B80u, 0x00030000u, 0x00033FFFu, true },  /* 9.4.84 EMI_DS_CTRL */
};

/* Section 9.4.2-9.4.15 MUXSELx field tables list BANKn_PINmm only for pins that exist (bank 0 has
   no pins 8-15 and 29-31; MUXSEL0 is 0x0000FFFF); section 9.4.36-9.4.42 PULLn writable masks and
   the DIN fields ("DIN ... corresponds to one of the 29 pins in bank 0") give the same sets. */
constexpr uint32_t kImx28PinctrlPinMask[5] = {
    0x1FFF00FFu, 0xFFFFFFFFu, 0x0F3FF7FFu, 0x7FF7FFFFu, 0x0011FFFFu,
};

}
