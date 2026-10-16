#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX51RM Table 59-5, Figures 59-3 to 59-18; MCIMX31RM Figure 31-9 (UFCR at 0x90, the same fields). */
namespace cerf_freescale_uart {

constexpr uint32_t kOffUrxd  = 0x00u;
constexpr uint32_t kOffUtxd  = 0x40u;
constexpr uint32_t kOffUcr1  = 0x80u;
constexpr uint32_t kOffUcr2  = 0x84u;
constexpr uint32_t kOffUcr3  = 0x88u;
constexpr uint32_t kOffUcr4  = 0x8Cu;
constexpr uint32_t kOffUfcr  = 0x90u;
constexpr uint32_t kOffUsr1  = 0x94u;
constexpr uint32_t kOffUsr2  = 0x98u;
constexpr uint32_t kOffUesc  = 0x9Cu;
constexpr uint32_t kOffUtim  = 0xA0u;
constexpr uint32_t kOffUbir  = 0xA4u;
constexpr uint32_t kOffUbmr  = 0xA8u;
constexpr uint32_t kOffUbrc  = 0xACu;
constexpr uint32_t kOffOnems = 0xB0u;
constexpr uint32_t kOffUts   = 0xB4u;
constexpr uint32_t kCtrlLo   = 0x80u;
constexpr uint32_t kCtrlHi   = 0xB8u;

constexpr uint32_t kUcr1Aden     = 1u << 15;
constexpr uint32_t kUcr1Adbr     = 1u << 14;
constexpr uint32_t kUcr1Trdyen   = 1u << 13;
constexpr uint32_t kUcr1Iden     = 1u << 12;
constexpr uint32_t kUcr1IcdShift = 10;
constexpr uint32_t kUcr1Rrdyen   = 1u << 9;
constexpr uint32_t kUcr1Rxdmaen  = 1u << 8;
constexpr uint32_t kUcr1Iren     = 1u << 7;
constexpr uint32_t kUcr1Txmptyen = 1u << 6;
constexpr uint32_t kUcr1Rtsden   = 1u << 5;
constexpr uint32_t kUcr1Sndbrk   = 1u << 4;
constexpr uint32_t kUcr1Txdmaen  = 1u << 3;
constexpr uint32_t kUcr1Atdmaen  = 1u << 2;
constexpr uint32_t kUcr1Doze     = 1u << 1;
constexpr uint32_t kUcr1Uarten   = 1u << 0;

constexpr uint32_t kUcr2Esci  = 1u << 15;
constexpr uint32_t kUcr2Escen = 1u << 11;
constexpr uint32_t kUcr2Pren  = 1u << 8;
constexpr uint32_t kUcr2Proe  = 1u << 7;
constexpr uint32_t kUcr2Stpb  = 1u << 6;
constexpr uint32_t kUcr2Ws    = 1u << 5;
constexpr uint32_t kUcr2Rtsen = 1u << 4;
constexpr uint32_t kUcr2Aten  = 1u << 3;
constexpr uint32_t kUcr2Txen  = 1u << 2;
constexpr uint32_t kUcr2Rxen  = 1u << 1;
constexpr uint32_t kUcr2Srst  = 1u << 0;

constexpr uint32_t kUcr3Dtren    = 1u << 13;
constexpr uint32_t kUcr3Parerren = 1u << 12;
constexpr uint32_t kUcr3Fraerren = 1u << 11;
constexpr uint32_t kUcr3Dcd      = 1u << 9;
constexpr uint32_t kUcr3Ri       = 1u << 8;
constexpr uint32_t kUcr3Adnimp   = 1u << 7;
constexpr uint32_t kUcr3Rxdsen   = 1u << 6;
constexpr uint32_t kUcr3Airinten = 1u << 5;
constexpr uint32_t kUcr3Awaken   = 1u << 4;
constexpr uint32_t kUcr3Dtrden   = 1u << 3;
constexpr uint32_t kUcr3Invt     = 1u << 1;
constexpr uint32_t kUcr3Acien    = 1u << 0;

constexpr uint32_t kUcr4Invr    = 1u << 9;
constexpr uint32_t kUcr4Eniri   = 1u << 8;
constexpr uint32_t kUcr4Wken    = 1u << 7;
constexpr uint32_t kUcr4Iddmaen = 1u << 6;
constexpr uint32_t kUcr4Tcen    = 1u << 3;
constexpr uint32_t kUcr4Bken    = 1u << 2;
constexpr uint32_t kUcr4Oren    = 1u << 1;
constexpr uint32_t kUcr4Dren    = 1u << 0;

constexpr uint32_t kUfcrTxtlShift  = 10;
constexpr uint32_t kUfcrRfdivShift = 7;
constexpr uint32_t kUfcrDcedte     = 1u << 6;
constexpr uint32_t kUfcrLevelMask  = 0x3Fu;

constexpr uint32_t kUsr1Parityerr = 1u << 15;
constexpr uint32_t kUsr1Rtss      = 1u << 14;
constexpr uint32_t kUsr1Trdy      = 1u << 13;
constexpr uint32_t kUsr1Rtsd      = 1u << 12;
constexpr uint32_t kUsr1Escf      = 1u << 11;
constexpr uint32_t kUsr1Framerr   = 1u << 10;
constexpr uint32_t kUsr1Rrdy      = 1u << 9;
constexpr uint32_t kUsr1Agtim     = 1u << 8;
constexpr uint32_t kUsr1Dtrd      = 1u << 7;
constexpr uint32_t kUsr1Rxds      = 1u << 6;
constexpr uint32_t kUsr1Airint    = 1u << 5;
constexpr uint32_t kUsr1Awake     = 1u << 4;

constexpr uint32_t kUsr2Adet    = 1u << 15;
constexpr uint32_t kUsr2Txfe    = 1u << 14;
constexpr uint32_t kUsr2Dtrf    = 1u << 13;
constexpr uint32_t kUsr2Idle    = 1u << 12;
constexpr uint32_t kUsr2Acst    = 1u << 11;
constexpr uint32_t kUsr2Ridelt  = 1u << 10;
constexpr uint32_t kUsr2Riin    = 1u << 9;
constexpr uint32_t kUsr2Irint   = 1u << 8;
constexpr uint32_t kUsr2Wake    = 1u << 7;
constexpr uint32_t kUsr2Dcddelt = 1u << 6;
constexpr uint32_t kUsr2Dcdin   = 1u << 5;
constexpr uint32_t kUsr2Rtsf    = 1u << 4;
constexpr uint32_t kUsr2Txdc    = 1u << 3;
constexpr uint32_t kUsr2Brcd    = 1u << 2;
constexpr uint32_t kUsr2Ore     = 1u << 1;
constexpr uint32_t kUsr2Rdr     = 1u << 0;

constexpr uint32_t kUrxdCharrdy = 1u << 15;
constexpr uint32_t kUrxdErr     = 1u << 14;
constexpr uint32_t kUrxdOvrrun  = 1u << 13;
constexpr uint32_t kUrxdFrmerr  = 1u << 12;
constexpr uint32_t kUrxdBrk     = 1u << 11;
constexpr uint32_t kUrxdPrerr   = 1u << 10;

constexpr uint32_t kUtsFrcperr = 1u << 13;
constexpr uint32_t kUtsLoop    = 1u << 12;
constexpr uint32_t kUtsLoopir  = 1u << 10;
constexpr uint32_t kUtsTxempty = 1u << 6;
constexpr uint32_t kUtsRxempty = 1u << 5;
constexpr uint32_t kUtsTxfull  = 1u << 4;
constexpr uint32_t kUtsRxfull  = 1u << 3;

constexpr uint32_t kFifoDepth = 32u;
constexpr uint32_t kBitTicks  = 16u;

/* Control/baud regs 0x80..0xB8 step 4, reset values (MCIMX51RM Table 59-3 /
   MCIMX31RM Table 31-2). ONEMS (0xB0) is the only 24-bit register. */
constexpr uint32_t kCtrlCount = 15u;
constexpr uint32_t kCtrlReset[kCtrlCount] = {
    0x0000u, 0x0001u, 0x0700u, 0x8000u, 0x0801u,
    0x2040u, 0x4028u, 0x002Bu, 0x0000u, 0x0000u,
    0x0000u, 0x0004u, 0x0000u, 0x0060u, 0x0000u,
};
constexpr uint32_t kUtsWritable = 0x3E00u;
constexpr uint32_t kUescMask    = 0x00FFu;
constexpr uint32_t kUtimMask    = 0x0FFFu;

inline uint32_t TxtlOf(uint32_t ufcr) { return (ufcr >> kUfcrTxtlShift) & kUfcrLevelMask; }

inline uint32_t RxtlOf(uint32_t ufcr) {
    const uint32_t t = ufcr & kUfcrLevelMask;
    return t == 0u ? 1u : t;
}

/* MCIMX51RM Table 59-12 RFDIV (page 59-22): "The input clock is module_clock. The output from the
   divider is ref_clk"; 000 /6, 001 /5, 010 /4, 011 /3, 100 /2, 101 /1, 110 /7, 111 Reserved. */
inline uint32_t RefDivider(uint32_t ufcr) {
    static constexpr uint32_t kDividers[8] = {6u, 5u, 4u, 3u, 2u, 1u, 7u, 1u};
    return kDividers[(ufcr >> kUfcrRfdivShift) & 0x7u];
}

struct FreescaleUartFrame {
    uint32_t data_bits = 8;
    uint32_t bits      = 10;
    bool     parity    = false;
    bool     odd       = false;
};

inline FreescaleUartFrame FrameOf(uint32_t ucr2) {
    FreescaleUartFrame f;
    f.data_bits = (ucr2 & kUcr2Ws) != 0u ? 8u : 7u;
    f.parity    = (ucr2 & kUcr2Pren) != 0u;
    f.odd       = (ucr2 & kUcr2Proe) != 0u;
    f.bits      = 1u + f.data_bits + (f.parity ? 1u : 0u) + ((ucr2 & kUcr2Stpb) != 0u ? 2u : 1u);
    return f;
}

/* MCIMX51RM Figure 59-24 (page 59-61): start bit 0, data bits from bit 0, the optional parity bit,
   stop bits 1; Table 59-9 PROE: "When PROE is high, odd parity is generated and expected". Bit i of
   the result is the line level during bit time i of the frame. */
inline uint32_t WireBits(uint8_t byte, bool parity_error, bool brk, const FreescaleUartFrame& f) {
    if (brk) return 0u;
    const uint32_t data = byte & ((1u << f.data_bits) - 1u);
    uint32_t bits = data << 1;
    uint32_t pos  = 1u + f.data_bits;
    if (f.parity) {
        uint32_t ones = 0u;
        for (uint32_t v = data; v != 0u; v &= v - 1u) ++ones;
        const bool level = ((ones & 1u) != 0u) != f.odd;
        if (level != parity_error) bits |= 1u << pos;
        ++pos;
    }
    for (; pos < f.bits; ++pos) bits |= 1u << pos;
    return bits;
}

struct FreescaleUartSource {
    uint32_t enable_reg;
    uint32_t enable_bit;
    bool     usr2;
    uint32_t flag_bit;
};

/* MCIMX51RM Table 59-22, page 59-33 and page 59-34: the interrupt_uart rows. Table 59-27 (page
   59-48): ADET interrupts only with ADBR set. */
inline constexpr FreescaleUartSource kInterruptSources[] = {
    {kOffUcr1, kUcr1Rrdyen,   false, kUsr1Rrdy},      {kOffUcr1, kUcr1Iden,     true,  kUsr2Idle},
    {kOffUcr4, kUcr4Dren,     true,  kUsr2Rdr},       {kOffUcr3, kUcr3Rxdsen,   false, kUsr1Rxds},
    {kOffUcr2, kUcr2Aten,     false, kUsr1Agtim},     {kOffUcr1, kUcr1Txmptyen, true,  kUsr2Txfe},
    {kOffUcr1, kUcr1Trdyen,   false, kUsr1Trdy},      {kOffUcr4, kUcr4Tcen,     true,  kUsr2Txdc},
    {kOffUcr4, kUcr4Oren,     true,  kUsr2Ore},       {kOffUcr4, kUcr4Bken,     true,  kUsr2Brcd},
    {kOffUcr4, kUcr4Wken,     true,  kUsr2Wake},      {kOffUcr1, kUcr1Aden,     true,  kUsr2Adet},
    {kOffUcr3, kUcr3Acien,    true,  kUsr2Acst},      {kOffUcr2, kUcr2Esci,     false, kUsr1Escf},
    {kOffUcr4, kUcr4Eniri,    true,  kUsr2Irint},     {kOffUcr3, kUcr3Airinten, false, kUsr1Airint},
    {kOffUcr3, kUcr3Awaken,   false, kUsr1Awake},     {kOffUcr3, kUcr3Fraerren, false, kUsr1Framerr},
    {kOffUcr3, kUcr3Parerren, false, kUsr1Parityerr}, {kOffUcr1, kUcr1Rtsden,   false, kUsr1Rtsd},
    {kOffUcr2, kUcr2Rtsen,    true,  kUsr2Rtsf},      {kOffUcr3, kUcr3Dtren,    true,  kUsr2Dtrf},
    {kOffUcr3, kUcr3Ri,       true,  kUsr2Ridelt},    {kOffUcr3, kUcr3Dcd,      true,  kUsr2Dcddelt},
    {kOffUcr3, kUcr3Dtrden,   false, kUsr1Dtrd},
};

inline uint32_t CtrlOf(const uint32_t (&ctrl)[kCtrlCount], uint32_t off) {
    return ctrl[(off - kCtrlLo) / 4u];
}

inline bool IrMode(const uint32_t (&ctrl)[kCtrlCount]) {
    return (CtrlOf(ctrl, kOffUcr1) & kUcr1Iren) != 0u;
}

/* MCIMX51RM Table 59-21: LOOP connects the transmitter output to the receiver input; LOOPIR
   "Connect IR transmitter to IR receiver". */
inline bool RxInputLooped(const uint32_t (&ctrl)[kCtrlCount]) {
    const uint32_t uts = CtrlOf(ctrl, kOffUts);
    return (uts & kUtsLoop) != 0u || (IrMode(ctrl) && (uts & kUtsLoopir) != 0u);
}

/* MCIMX51RM sections 59.4.7.1 and 59.4.7.2 (page 59-54): INVT and INVR set the pulse polarity of
   the transmit and the receive path. */
inline bool TxLoopsToRx(const uint32_t (&ctrl)[kCtrlCount]) {
    if ((CtrlOf(ctrl, kOffUts) & kUtsLoop) != 0u) return true;
    const bool invt = (CtrlOf(ctrl, kOffUcr3) & kUcr3Invt) != 0u;
    const bool invr = (CtrlOf(ctrl, kOffUcr4) & kUcr4Invr) != 0u;
    return RxInputLooped(ctrl) && invt != invr;
}

inline bool InterruptAsserted(const uint32_t (&ctrl)[kCtrlCount], uint32_t usr1, uint32_t usr2) {
    if ((CtrlOf(ctrl, kOffUcr1) & kUcr1Adbr) == 0u) usr2 &= ~kUsr2Adet;
    for (const FreescaleUartSource& s : kInterruptSources) {
        if ((CtrlOf(ctrl, s.enable_reg) & s.enable_bit) != 0u &&
            ((s.usr2 ? usr2 : usr1) & s.flag_bit) != 0u) {
            return true;
        }
    }
    return false;
}

}

}
