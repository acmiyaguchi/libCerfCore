#include "imx28_duart.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../tracing/kernel_debug_sink.h"
#include "imx28_id.h"
#include "imx28_pinctrl.h"

namespace cerfcore {

namespace {

/* MCIMX28RM section 24.3 HW_UARTDBG memory map (no SCT registers are listed). */
constexpr uint32_t kDr    = 0x00u;
constexpr uint32_t kEcr   = 0x04u;
constexpr uint32_t kFr    = 0x18u;
constexpr uint32_t kIlpr  = 0x20u;
constexpr uint32_t kIbrd  = 0x24u;
constexpr uint32_t kFbrd  = 0x28u;
constexpr uint32_t kLcrH  = 0x2Cu;
constexpr uint32_t kCr    = 0x30u;
constexpr uint32_t kIfls  = 0x34u;
constexpr uint32_t kImsc  = 0x38u;
constexpr uint32_t kRis   = 0x3Cu;
constexpr uint32_t kMis   = 0x40u;
constexpr uint32_t kIcr   = 0x44u;
constexpr uint32_t kDmacr = 0x48u;

/* Section 24.3.3 FR reset 0x90: TXFE [7] and RXFE [4]; CTS [0] is "the complement of the UART
   clear to send" input. Section 9.4.8 MUXSEL6 BANK3_PIN00: 10 = duart_cts. */
constexpr uint32_t kFrReset   = 0x90u;
constexpr uint32_t kFrCts     = 1u << 0;
constexpr uint32_t kCtsBank   = 3u;
constexpr uint32_t kCtsPin    = 0u;
constexpr uint32_t kCtsMux    = 2u;

/* Section 24.3.8 CR reset 0x0300: RXE [9], TXE [8]; UARTEN [0], SIREN [1], SIRLP [2],
   LBE [7], CTSEN [15]. */
constexpr uint32_t kCrReset   = 0x0300u;
constexpr uint32_t kCrUarten  = 1u << 0;
constexpr uint32_t kCrSiren   = 1u << 1;
constexpr uint32_t kCrSirlp   = 1u << 2;
constexpr uint32_t kCrLbe     = 1u << 7;
constexpr uint32_t kCrTxe     = 1u << 8;
constexpr uint32_t kCrCtsen   = 1u << 15;
constexpr uint32_t kCrWritable = 0xFF87u;   /* bits 6:3 reserved */

/* Section 24.3.9 IFLS reset 0x12; RXIFLSEL [5:3], TXIFLSEL [2:0], 0x5-0x7 INVALID. */
constexpr uint32_t kIflsReset = 0x12u;
constexpr uint32_t kIflsMask  = 0x3Fu;
constexpr uint32_t kIflsMaxSel = 4u;

/* Section 24.3.7 LCR_H FEN [4]; section 24.2.1: "The divisor must be between 0x00000040 and
   0x003FFFC0, inclusive". */
constexpr uint32_t kLcrHFen    = 1u << 4;
constexpr uint32_t kLcrHMask   = 0xFFu;
constexpr uint32_t kDivMin     = 0x40u;
constexpr uint32_t kDivMax     = 0x3FFFC0u;

/* Section 24.3.5 IBRD [15:0]; 24.3.6 FBRD [5:0]; 24.3.4 ILPR [7:0]; 24.3.14 DMACR [2:0];
   24.3.10 IMSC / 24.3.11 RIS / 24.3.13 ICR bits [10:0]; RIS TXRIS [5]. */
constexpr uint32_t kIbrdMask  = 0xFFFFu;
constexpr uint32_t kFbrdMask  = 0x3Fu;
constexpr uint32_t kIlprMask  = 0xFFu;
constexpr uint32_t kDmacrMask = 0x7u;
constexpr uint32_t kIntMask   = 0x7FFu;
constexpr uint32_t kTxInt     = 1u << 5;

}  /* namespace */

bool Imx28Duart::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Duart::OnReady() {
    LoadResetValues();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { LoadResetValues(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Duart::LoadResetValues() {
    FlushLine();
    ilpr_ = ibrd_ = fbrd_ = lcr_h_ = div_ = imsc_ = ris_ = dmacr_ = 0u;
    cr_ = kCrReset;
    ifls_ = kIflsReset;
}

void Imx28Duart::FlushLine() {
    emu_.Get<KernelDebugSink>().EmitLine(tx_line_, "DUART", true);
    tx_line_.clear();
}

/* Section 24.3.12: MIS is RIS gated by IMSC; section 24.1: one combined interrupt, asserted when
   any individual interrupt is asserted and unmasked. */
void Imx28Duart::CheckInterruptLine(const char* cause) {
    const uint32_t mis = ris_ & imsc_;
    if (mis != 0u) {
        emu_.Get<Fatal>().Die("Imx28Duart: interrupt asserted unmasked after %s (RIS %03X IMSC %03X); "
                              "no ICOLL to deliver it", cause, ris_, imsc_);
    }
}

/* Figure 24-1 shows only UART1RXD, UART1TXD, UART2RTS and UART2CTS pins on the Debug UART, so
   DSR, DCD and RI have no input and keep the section 24.3.3 reset value 0. nUARTCTS is wired to
   its pad only while BANK3_PIN00 is muxed to duart_cts, otherwise CTS holds the reset value 0. */
uint32_t Imx28Duart::ReadFr() {
    auto* pins = emu_.TryGet<Imx28Pinctrl>();
    if (pins == nullptr || pins->MuxSelect(kCtsBank, kCtsPin) != kCtsMux) return kFrReset;
    return pins->SamplePin(kCtsBank, kCtsPin, "Imx28Duart FR.CTS") ? kFrReset : (kFrReset | kFrCts);
}

uint32_t Imx28Duart::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        case kDr:    return 0u;          /* section 24.3.1 reset 0 */
        case kEcr:   return 0u;          /* section 24.3.2 reset 0 */
        case kFr:    return ReadFr();
        case kIlpr:  return ilpr_;
        case kIbrd:  return ibrd_;
        case kFbrd:  return fbrd_;
        case kLcrH:  return lcr_h_;
        case kCr:    return cr_;
        case kIfls:  return ifls_;
        case kImsc:  return imsc_;
        case kRis:   return ris_;
        case kMis:   return ris_ & imsc_;
        case kIcr:   return 0u;          /* section 24.3.13 write-only, reset 0, bits 15:11 read as zero */
        case kDmacr: return dmacr_;
        default:     HaltUnsupportedAccess("ReadWord", addr, 0);
    }
}

/* MCIMX28RM section 24.3.1 (DR write pushes the transmit FIFO), 24.3.8 (TXE and UARTEN must
   both be 1 to transmit; LBE, CTSEN, SIREN, SIRLP alter the line). */
void Imx28Duart::Transmit(uint8_t ch) {
    if ((cr_ & (kCrUarten | kCrTxe)) != (kCrUarten | kCrTxe)) {
        emu_.Get<Fatal>().Die("Imx28Duart: DR written with CR %04X (UARTEN and TXE required)", cr_);
    }
    if (div_ < kDivMin || div_ > kDivMax) {
        emu_.Get<Fatal>().Die("Imx28Duart: transmit with latched baud divisor %06X outside "
                              "0x40..0x3FFFC0 (RM 24.2.1)", div_);
    }
    emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(ch), tx_line_, "DUART", true);
    /* DDI0183G section 2.8.3 UARTTXINTR: FIFOs disabled, holding register empties. */
    if ((lcr_h_ & kLcrHFen) == 0u) {
        ris_ |= kTxInt;
        CheckInterruptLine("transmit");
    }
}

void Imx28Duart::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    switch (off) {
        case kDr:
            Transmit(static_cast<uint8_t>(value));
            return;
        case kEcr:
            return;                      /* section 24.3.2: any write clears the error bits */
        case kIlpr:  ilpr_ = value & kIlprMask; return;
        case kIbrd:  ibrd_ = value & kIbrdMask; return;
        case kFbrd:  fbrd_ = value & kFbrdMask; return;
        case kLcrH: {
            div_ = (ibrd_ << 6) | fbrd_;   /* 24.2.1: IBRD/FBRD update on the LCR_H write strobe */
            lcr_h_ = value & kLcrHMask;
            return;
        }
        case kCr: {
            if ((value & (kCrLbe | kCrCtsen | kCrSiren | kCrSirlp)) != 0u) {
                emu_.Get<Fatal>().Die("Imx28Duart: CR %04X enables loopback / CTS flow control / SIR",
                                      value);
            }
            cr_ = value & kCrWritable;
            return;
        }
        case kIfls:
            if (((value >> 3) & 0x7u) > kIflsMaxSel || (value & 0x7u) > kIflsMaxSel) {
                emu_.Get<Fatal>().Die("Imx28Duart: IFLS %02X selects an INVALID level (RM 24.3.9)",
                                      value);
            }
            ifls_ = value & kIflsMask;
            return;
        case kImsc:
            imsc_ = value & kIntMask;
            CheckInterruptLine("IMSC write");
            return;
        case kIcr:
            ris_ &= ~(value & kIntMask);
            return;
        case kDmacr: dmacr_ = value & kDmacrMask; return;
        default:     HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

uint8_t Imx28Duart::ReadByte(uint32_t addr) {
    const uint32_t shift = (addr & 0x3u) * 8u;
    return static_cast<uint8_t>((ReadWord(addr & ~0x3u) >> shift) & 0xFFu);
}

uint16_t Imx28Duart::ReadHalf(uint32_t addr) {
    if ((addr & 0x1u) != 0) HaltUnsupportedAccess("ReadHalf-unaligned", addr, 0);
    const uint32_t shift = (addr & 0x2u) * 8u;
    return static_cast<uint16_t>((ReadWord(addr & ~0x3u) >> shift) & 0xFFFFu);
}

/* Section 24.3.1 DR: "The UART IP only implements 16 and 8-bit registers"; only DATA [7:0] of DR
   is a transmit field, so a sub-word DR write must carry its value in byte lane 0. */
void Imx28Duart::WriteSub(uint32_t addr, uint32_t shift, uint32_t lane_mask, uint32_t value) {
    const uint32_t base = addr & ~0x3u;
    if (base - MmioBase() == kDr) {
        if (shift != 0u || (value & ~0xFFu) != 0u) {
            HaltUnsupportedAccess("WriteSub-DR-lane", addr, value);
        }
        WriteWord(base, value);
        return;
    }
    const uint32_t old = ReadWord(base);
    WriteWord(base, (old & ~(lane_mask << shift)) | (value << shift));
}

void Imx28Duart::WriteByte(uint32_t addr, uint8_t value) {
    WriteSub(addr, (addr & 0x3u) * 8u, 0xFFu, value);
}

void Imx28Duart::WriteHalf(uint32_t addr, uint16_t value) {
    if ((addr & 0x1u) != 0) HaltUnsupportedAccess("WriteHalf-unaligned", addr, value);
    WriteSub(addr, (addr & 0x2u) * 8u, 0xFFFFu, value);
}

void Imx28Duart::SaveState(StateWriter& w) {
    w.WriteBytes("ilpr", &ilpr_, sizeof(ilpr_));
    w.WriteBytes("ibrd", &ibrd_, sizeof(ibrd_));
    w.WriteBytes("div", &div_, sizeof(div_));
    w.WriteBytes("fbrd", &fbrd_, sizeof(fbrd_));
    w.WriteBytes("lcr_h", &lcr_h_, sizeof(lcr_h_));
    w.WriteBytes("cr", &cr_, sizeof(cr_));
    w.WriteBytes("ifls", &ifls_, sizeof(ifls_));
    w.WriteBytes("imsc", &imsc_, sizeof(imsc_));
    w.WriteBytes("ris", &ris_, sizeof(ris_));
    w.WriteBytes("dmacr", &dmacr_, sizeof(dmacr_));
}

void Imx28Duart::RestoreState(StateReader& r) {
    r.ReadBytes("ilpr", &ilpr_, sizeof(ilpr_));
    r.ReadBytes("ibrd", &ibrd_, sizeof(ibrd_));
    r.ReadBytes("div", &div_, sizeof(div_));
    r.ReadBytes("fbrd", &fbrd_, sizeof(fbrd_));
    r.ReadBytes("lcr_h", &lcr_h_, sizeof(lcr_h_));
    r.ReadBytes("cr", &cr_, sizeof(cr_));
    r.ReadBytes("ifls", &ifls_, sizeof(ifls_));
    r.ReadBytes("imsc", &imsc_, sizeof(imsc_));
    r.ReadBytes("ris", &ris_, sizeof(ris_));
    r.ReadBytes("dmacr", &dmacr_, sizeof(dmacr_));
    tx_line_.clear();
}

REGISTER_SERVICE(Imx28Duart);

}
