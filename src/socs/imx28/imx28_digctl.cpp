#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../oscillator_ticks.h"
#include "imx28_digctl.h"
#include "imx28_id.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Section 19.4 memory map. */
constexpr uint32_t kCtrl            = 0x000u;
constexpr uint32_t kStatus          = 0x010u;
constexpr uint32_t kRamctrl         = 0x030u;
constexpr uint32_t kReadMargin      = 0x050u;
constexpr uint32_t kWriteOnce       = 0x060u;
constexpr uint32_t kBistCtl         = 0x070u;
constexpr uint32_t kBistStatus      = 0x080u;
constexpr uint32_t kMicroseconds    = 0x0C0u;
constexpr uint32_t kDbgrd           = 0x0D0u;
constexpr uint32_t kDbg             = 0x0E0u;
constexpr uint32_t kUsbLoopback     = 0x100u;
constexpr uint32_t kOcramStatus0    = 0x110u;
constexpr uint32_t kOcramStatusEnd  = 0x1F0u;
constexpr uint32_t kScratch0        = 0x280u;
constexpr uint32_t kScratch1        = 0x290u;
constexpr uint32_t kArmCache        = 0x2A0u;
constexpr uint32_t kDebugTrap       = 0x2B0u;
constexpr uint32_t kTrapAddr0       = 0x2C0u;
constexpr uint32_t kTrapAddrEnd     = 0x300u;
constexpr uint32_t kFsl             = 0x300u;
constexpr uint32_t kChipId          = 0x310u;
constexpr uint32_t kMpte0           = 0x500u;
constexpr uint32_t kMpteEnd         = 0x600u;
constexpr uint32_t kMapEnd          = 0x600u;
constexpr uint32_t kRegStride       = 0x010u;

/* Section 19.4.1 CTRL reset 0001_0004h: XTAL24M_GATE [30], USB1_OVERCURRENT_ENABLE [24] to
   USB1_CLKGATE [16], SAIF_LOOPBACK [15], DUART_LOOPBACK [14], AUART01_LOOPBACK [13],
   SAIF_CLKMUX_SEL [11:10], DEBUG_DISABLE [3], USB0_CLKGATE [2], JTAG_SHIELD [1], LATCH_ENTROPY [0]. */
constexpr uint32_t kCtrlReset       = 0x00010004u;
constexpr uint32_t kCtrlWritable    = 0x41FFEC0Fu;
constexpr uint32_t kCtrlXtalGate    = 1u << 30;
constexpr uint32_t kCtrlUsbGates    = 0x00010004u;
constexpr uint32_t kCtrlSaifClkmux  = 0x00000C00u;
constexpr uint32_t kCtrlJtagShield  = 1u << 1;
constexpr uint32_t kCtrlLatchEntropy = 1u << 0;

/* Section 19.4.2 STATUS reset FF00_0000h: USB0/USB1 *_PRESENT [31:24], WRITTEN [0]. */
constexpr uint32_t kStatusReset     = 0xFF000000u;
constexpr uint32_t kStatusWritten   = 1u << 0;

/* Section 19.4.4 RAMCTRL reset 0; 19.4.6 READ_MARGIN reset 0000_0002h; 19.4.8 BIST_CTL reset
   4000_0000h. */
constexpr uint32_t kRamctrlWritable = 0x00003F00u;
constexpr uint32_t kReadMarginReset = 0x00000002u;
constexpr uint32_t kReadMarginMask  = 0x0000000Fu;
constexpr uint32_t kBistReset       = 0x40000000u;
constexpr uint32_t kBistWritable    = 0xF80007FFu;

/* Section 19.4.7 WRITEONCE reset A5A5_A5A5h. */
constexpr uint32_t kWriteOnceReset  = 0xA5A5A5A5u;

/* Section 19.4.12 MICROSECONDS: "a read-only test value register"; 24.0 MHz divided by 24. */
constexpr uint64_t kMicrosecondHz   = 1000000u;

/* Sections 19.4.13 DBGRD, 19.4.14 DBG, 19.4.39 CHIPID reset 2800_0000h. */
constexpr uint32_t kDbgrdValue      = 0x789ABCDEu;
constexpr uint32_t kDbgValue        = 0x87654321u;
constexpr uint32_t kChipIdValue     = 0x28000000u;

/* Section 19.4.15 USB_LOOPBACK control bits [17:4]; section 19.4.32 ARMCACHE timing fields. */
constexpr uint32_t kUsbLoopbackWritable = 0x0003FFF0u;
constexpr uint32_t kArmCacheWritable    = 0x00033333u;

/* Section 19.4.33 DEBUG_TRAP: TRAP_IN_RANGE [1], TRAP_ENABLE [0]. */
constexpr uint32_t kTrapWritable    = 0x3u;

/* Section 19.4.38 FSL: words read back at 0x300, 0x304 and 0x308. */
constexpr uint32_t kFslWords[3]     = {0x65657246u, 0x6C616373u, 0xAEA92D65u};

/* Sections 19.4.50-19.4.65 MPTEn_LOC reset n: DIS [31], SPAN [26:24], LOC [11:0]. */
constexpr uint32_t kMpteWritable    = 0x87000FFFu;
constexpr uint32_t kAllBits         = 0xFFFFFFFFu;

/* Section 39.3: the registers that list _SET, _CLR and _TOG and are not read-only. */
bool HasSctAlias(uint32_t reg) {
    return reg == kCtrl || reg == kRamctrl || reg == kReadMargin || reg == kBistCtl ||
           reg == kMicroseconds || reg == kUsbLoopback || reg == kDebugTrap;
}

}  /* namespace */

bool Imx28Digctl::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Digctl::OnReady() {
    us_.Attach(kMicrosecondHz, 1u);
    LoadResetValues();
    emu_.Get<GuestCycleClock>().RegisterRateListener([this] { us_.Rescale(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { LoadResetValues(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* Sections 19.4 register tables, Reset value column; section 19.4.12: the count is not
   preserved over power-downs. */
void Imx28Digctl::LoadResetValues() {
    ctrl_       = kCtrlReset;
    write_once_ = kWriteOnceReset;
    for (uint32_t& s : scratch_) s = 0u;
    written_    = false;
    us_.SetCounting(true);
    us_base_    = 0u - static_cast<uint32_t>(us_.Now());
}

uint32_t Imx28Digctl::Microseconds() { return us_base_ + static_cast<uint32_t>(us_.Now()); }

uint32_t Imx28Digctl::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    if (off >= kFsl && off < kFsl + sizeof(kFslWords)) return kFslWords[(off - kFsl) / 4u];
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(a.reg_offset)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    const uint32_t reg = a.reg_offset;
    switch (reg) {
        case kCtrl:         return ctrl_;
        case kStatus:       return kStatusReset | (written_ ? kStatusWritten : 0u);
        case kRamctrl:      return 0u;
        case kReadMargin:   return kReadMarginReset;
        case kWriteOnce:    return write_once_;
        case kBistCtl:      return kBistReset;
        case kBistStatus:   return 0u;
        case kMicroseconds: return Microseconds();
        case kDbgrd:        return kDbgrdValue;
        case kDbg:          return kDbgValue;
        case kUsbLoopback:  return 0u;
        case kScratch0:     return scratch_[0];
        case kScratch1:     return scratch_[1];
        case kArmCache:     return 0u;
        case kDebugTrap:    return 0u;
        case kChipId:       return kChipIdValue;
        default:            break;
    }
    if (reg >= kOcramStatus0 && reg < kOcramStatusEnd) return 0u;
    if (reg >= kTrapAddr0 && reg < kTrapAddrEnd) return 0u;
    if (reg >= kMpte0 && reg < kMpteEnd) return (reg - kMpte0) / kRegStride;
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

/* Section 19.4.1 CTRL: XTAL24M_GATE [30], USB1_CLKGATE [16], SAIF_CLKMUX_SEL [11:10],
   USB0_CLKGATE [2], JTAG_SHIELD [1], LATCH_ENTROPY [0]. */
void Imx28Digctl::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    const uint32_t raw  = Imx28ApplySct(op, old, value);
    const uint32_t modelled =
        kCtrlXtalGate | kCtrlUsbGates | kCtrlSaifClkmux | kCtrlJtagShield | kCtrlLatchEntropy;
    if (((raw ^ kCtrlReset) & kCtrlWritable & ~modelled) != 0u) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    ctrl_ = (old & ~kCtrlWritable) | (raw & kCtrlWritable);
    us_.SetCounting((ctrl_ & kCtrlXtalGate) == 0u);
}

void Imx28Digctl::RequireReset(Imx28SctOp op, uint32_t reset, uint32_t writable, uint32_t value,
                               uint32_t addr) {
    if (((Imx28ApplySct(op, reset, value) ^ reset) & writable) != 0u) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
}

void Imx28Digctl::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t reg = a.reg_offset;
    if (a.op != Imx28SctOp::Write && !HasSctAlias(reg)) HaltUnsupportedAccess("WriteWord", addr, value);
    switch (reg) {
        case kCtrl:
            WriteCtrl(a.op, value, addr);
            return;
        case kRamctrl:
            RequireReset(a.op, 0u, kRamctrlWritable, value, addr);
            return;
        case kReadMargin:
            RequireReset(a.op, kReadMarginReset, kReadMarginMask, value, addr);
            return;
        case kWriteOnce:
            if (!written_) {
                write_once_ = value;
                written_    = true;
            }
            return;
        case kBistCtl:
            RequireReset(a.op, kBistReset, kBistWritable, value, addr);
            return;
        case kUsbLoopback:
            RequireReset(a.op, 0u, kUsbLoopbackWritable, value, addr);
            return;
        case kScratch0:
        case kScratch1:
            scratch_[(reg - kScratch0) / kRegStride] = value;
            return;
        case kArmCache:
            RequireReset(a.op, 0u, kArmCacheWritable, value, addr);
            return;
        case kDebugTrap:
            RequireReset(a.op, 0u, kTrapWritable, value, addr);
            return;
        default:
            break;
    }
    if (reg >= kTrapAddr0 && reg < kTrapAddrEnd) {
        RequireReset(a.op, 0u, kAllBits, value, addr);
        return;
    }
    if (reg >= kMpte0 && reg < kMpteEnd) {
        RequireReset(a.op, (reg - kMpte0) / kRegStride, kMpteWritable, value, addr);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28Digctl::SaveState(StateWriter& w) {
    us_.Save(w, "us_");
    w.Write("ctrl", ctrl_);
    w.Write("write_once", write_once_);
    w.Write("scratch", scratch_);
    w.Write("us_base", us_base_);
    w.Write<uint8_t>("written", written_ ? 1u : 0u);
}

void Imx28Digctl::RestoreState(StateReader& r) {
    us_.Restore(r, "us_");
    r.Read("ctrl", ctrl_);
    r.Read("write_once", write_once_);
    r.Read("scratch", scratch_);
    r.Read("us_base", us_base_);
    uint8_t written = 0u;
    r.Read("written", written);
    written_ = written != 0u;
}

REGISTER_SERVICE(Imx28Digctl);

}
