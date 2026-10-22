#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_id.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 chapter 13 (DCP) at 0x8002_8000. */
class Imx28Dcp : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80028000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void ResetBlock();
    void WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);

    uint32_t ctrl_ = 0u;
};

/* Section 13.3.1 HW_DCP_CTRL 0x000: reset F080_0000h; PRESENT_CRYPTO [29] and PRESENT_SHA [28]
   read-only, RSVD1 [27:24], RSVD0 [20:9], RSVD_CSC_INTERRUPT_ENABLE [8]. */
constexpr uint32_t kOffCtrl      = 0x000u;
constexpr uint32_t kCtrlReset    = 0xF0800000u;
constexpr uint32_t kCtrlWritable = 0xC0E000FFu;

}  /* namespace */

bool Imx28Dcp::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Dcp::OnReady() {
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Dcp::ResetBlock() { ctrl_ = kCtrlReset; }

/* Section 13.3.1; sharp_brain_pw_sh4 nk.exe 80211ad4. */
void Imx28Dcp::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl_;
    const uint32_t next = Imx28ApplySct(op, old, value);
    if ((next & ~kCtrlWritable) != (kCtrlReset & ~kCtrlWritable)) {
        emu_.Get<Fatal>().Die("Imx28 DCP: CTRL read-only or reserved bits at %08X (value %08X, "
                              "CTRL %08X)", addr, value, old);
    }
    if ((next & (kCtrlSftrst | kCtrlClkgate)) != (kCtrlSftrst | kCtrlClkgate)) {
        emu_.Get<Fatal>().Die("Imx28 DCP: CTRL %08X -> %08X at %08X (value %08X)", old, next,
                              addr, value);
    }
    ctrl_ = next;
}

uint32_t Imx28Dcp::ReadWord(uint32_t addr) {
    if (addr - MmioBase() != kOffCtrl) HaltUnsupportedAccess("ReadWord", addr, 0);
    return ctrl_;
}

void Imx28Dcp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= 0x10u || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteCtrl(Imx28DecodeSct(off).op, value, addr);
}

void Imx28Dcp::SaveState(StateWriter& w) { w.Write("ctrl", ctrl_); }

void Imx28Dcp::RestoreState(StateReader& r) { r.Read("ctrl", ctrl_); }

REGISTER_SERVICE(Imx28Dcp);

}
