#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 chapter 14 (DRAM controller) at 0x800E_0000. */
class Imx28Dram : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x800E0000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

protected:
    bool HasSct() const override { return false; }

private:
    void ResetBlock() {
        ctl17_ = 0u;
        ctl22_ = 0u;
    }

    uint32_t ctl17_ = 0u;
    uint32_t ctl22_ = 0u;
};

/* Section 14.8.18 HW_DRAM_CTL17 0x044: reset 0; AUTO_REFRESH_MODE [24], AREFRESH [16] write-only
   and reads 0, ENABLE_QUICK_SREFRESH [8], SREFRESH [0]. */
constexpr uint32_t kOffCtl17      = 0x044u;
constexpr uint32_t kCtl17Stored   = 0x01000101u;
constexpr uint32_t kCtl17Writable = 0x01010101u;

/* Section 14.8.20 HW_DRAM_CTL22 0x058: reset 0; LOWPOWER_REFRESH_ENABLE [19:16],
   LOWPOWER_CONTROL [12:8], LOWPOWER_AUTO_ENABLE [4:0]. */
constexpr uint32_t kOffCtl22      = 0x058u;
constexpr uint32_t kCtl22Writable = 0x000F1F1Fu;

}  /* namespace */

bool Imx28Dram::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Dram::OnReady() {
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Imx28Dram::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off == kOffCtl17) return ctl17_;
    if (off != kOffCtl22) HaltUnsupportedAccess("ReadWord", addr, 0);
    return ctl22_;
}

/* Sections 14.8.18, 14.8.20; sharp_brain_pw_sh4 nk.exe 8020ed38 (CTL17), 8021225c (CTL22). */
void Imx28Dram::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == kOffCtl17) {
        if ((value & ~kCtl17Writable) != 0u) {
            emu_.Get<Fatal>().Die("Imx28 DRAM: CTL17 reserved bits at %08X (value %08X, CTL17 %08X)",
                                  addr, value, ctl17_);
        }
        ctl17_ = value & kCtl17Stored;
        return;
    }
    if (off != kOffCtl22) HaltUnsupportedAccess("WriteWord", addr, value);
    if ((value & ~kCtl22Writable) != 0u) {
        emu_.Get<Fatal>().Die("Imx28 DRAM: CTL22 reserved bits at %08X (value %08X, CTL22 %08X)",
                              addr, value, ctl22_);
    }
    ctl22_ = value;
}

void Imx28Dram::SaveState(StateWriter& w) {
    w.Write("ctl17", ctl17_);
    w.Write("ctl22", ctl22_);
}

void Imx28Dram::RestoreState(StateReader& r) {
    r.Read("ctl17", ctl17_);
    r.Read("ctl22", ctl22_);
}

REGISTER_SERVICE(Imx28Dram);

}
