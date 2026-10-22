#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_brain_pw_sh4_family_id.h"
#include "../../state/state_boot_gate.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/imx28/imx28_clkctrl.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 nk.exe 8020ac44, 8020a7ec; RM 10.8.8, 23.4.3 */
constexpr uint32_t kXbusPa     = 0x80040070u;
constexpr uint32_t kBootXbus   = 0x00000001u;

/* sharp_brain_pw_sh4 SDU item 3 boot stream 00000adc..00000b98; RM 10.8.1, 10.8.6, 10.8.7,
   10.8.24, 10.8.26 */
constexpr uint32_t kPll0Ctrl0SetPa = 0x80040004u;
constexpr uint32_t kPll0Power      = 0x00020000u;
constexpr uint32_t kFrac0Pa        = 0x800401B0u;
constexpr uint32_t kFrac0ClrPa     = 0x800401B8u;
constexpr uint32_t kFrac0GateCpu   = 0x00000080u;
constexpr uint32_t kFrac0CpuMask   = 0x0000003Fu;
constexpr uint32_t kBootCpuFrac    = 0x00000012u;
constexpr uint32_t kCpuPa          = 0x80040050u;
constexpr uint32_t kBootCpu        = 0x00011002u;
constexpr uint32_t kHbusPa         = 0x80040060u;
constexpr uint32_t kBootHbus       = 0x00000002u;
constexpr uint32_t kClkseqClrPa    = 0x800401D8u;
constexpr uint32_t kBypassCpu      = 0x00040000u;

class SharpBrainPwSh4BootClocks : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        emu_.Get<StateBootGate>().RegisterColdHandoff([this] { Apply(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Apply(); });
    }

private:
    void Apply() {
        auto& clk = emu_.Get<Imx28Clkctrl>();
        clk.WriteWord(kPll0Ctrl0SetPa, kPll0Power);
        clk.WriteWord(kFrac0ClrPa, kFrac0GateCpu);
        clk.WriteWord(kFrac0Pa, (clk.ReadWord(kFrac0Pa) & ~kFrac0CpuMask) | kBootCpuFrac);
        clk.WriteWord(kCpuPa, kBootCpu);
        clk.WriteWord(kHbusPa, kBootHbus);
        clk.WriteWord(kClkseqClrPa, kBypassCpu);
        clk.WriteWord(kXbusPa, kBootXbus);
    }
};

}

REGISTER_SERVICE(SharpBrainPwSh4BootClocks);

}
