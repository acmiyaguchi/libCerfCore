#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_id.h"
#include "imx28_saif.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Section 35.3 memory map. */
constexpr uint32_t kCtrl    = 0x00u;
constexpr uint32_t kStat    = 0x10u;
constexpr uint32_t kData    = 0x20u;
constexpr uint32_t kVersion = 0x30u;
constexpr uint32_t kMapEnd  = 0x40u;

/* Section 35.3.1 CTRL: reset C000_0000h; RSRVD2 [23:21]. */
constexpr uint32_t kCtrlReset    = 0xC0000000u;
constexpr uint32_t kCtrlReserved = 0x00E00000u;

/* Section 35.3.1 FIFO_ERROR_IRQ_EN [25], FIFO_SERVICE_IRQ_EN [24], CHANNEL_NUM_SELECT [15:14]
   (11 reserved), READ_MODE [1], RUN [0]. */
constexpr uint32_t kCtrlIrqEnables   = 0x03000000u;
constexpr uint32_t kCtrlChannelMask  = 0x0000C000u;
constexpr uint32_t kCtrlChannelRsvd  = 0x0000C000u;
constexpr uint32_t kCtrlReadMode     = 1u << 1;
constexpr uint32_t kCtrlRun          = 1u << 0;

}  /* namespace */

bool Imx28Saif::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Saif::OnReady() {
    ctrl_ = kCtrlReset;
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ctrl_ = kCtrlReset; });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Saif::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 SAIF%u: %s at %08X (value %08X, CTRL %08X)", unit_, what, addr,
                          value, ctrl_);
}

void Imx28Saif::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        ctrl_ = kCtrlReset;
        return;
    }
    if ((next & kCtrlReserved) != 0u) Die("CTRL RSRVD2 bits", addr, value);
    if ((next & kCtrlIrqEnables) != 0u) Die("CTRL FIFO IRQ enable", addr, value);
    if ((next & kCtrlRun) != 0u) {
        if ((old & kCtrlRun) != 0u && next != old) Die("CTRL change with RUN set", addr, value);
        if (Gated(next)) Die("RUN with SFTRST or CLKGATE", addr, value);
        if ((next & kCtrlReadMode) != 0u) Die("RUN in READ_MODE", addr, value);
        if ((next & kCtrlChannelMask) == kCtrlChannelRsvd) {
            Die("RUN with CHANNEL_NUM_SELECT 3", addr, value);
        }
    }
    ctrl_ = next;
}

uint32_t Imx28Saif::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl && a.op == Imx28SctOp::Write) return ctrl_;
    switch (a.reg_offset) {
        case kStat:    Die("STAT read", addr, 0u);
        case kData:    Die("DATA read", addr, 0u);
        case kVersion: Die("VERSION read", addr, 0u);
        default:       break;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Saif::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    switch (a.reg_offset) {
        case kCtrl: WriteCtrl(a.op, value, addr); return;
        case kStat: Die("STAT write", addr, value);
        case kData: Die("DATA write", addr, value);
        default:    break;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28Saif::SaveState(StateWriter& w) {
    w.Write("ctrl", ctrl_);
}

void Imx28Saif::RestoreState(StateReader& r) {
    r.Read("ctrl", ctrl_);
}

}
