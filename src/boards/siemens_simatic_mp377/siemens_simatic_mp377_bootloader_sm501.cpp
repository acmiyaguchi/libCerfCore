#include "siemens_simatic_mp377_panel.h"
#include "../../peripherals/silicon_motion_sm501/siemens_simatic_mp377_sm501.h"
#include "../../peripherals/silicon_motion_sm501/siemens_simatic_mp377_sm501_internal.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/iop13xx/iop13xx_atu_state.h"
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mp377_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace siemens_simatic_mp377 {
namespace {

/* SM501 Databook v1.02 section 5 register map. */
constexpr uint32_t kDcPanelControl = 0x080000u;
constexpr uint32_t kDcPanelFbAddress = 0x08000Cu;
constexpr uint32_t kDcPanelFbOffset = 0x080010u;
constexpr uint32_t kDcPanelFbWidth = 0x080014u;

class SiemensSimaticMp377BootloaderState final : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
    }

    void OnReady() override {
        ApplyPreset();
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) ApplyPreset();
        });
    }

private:
    void ApplyPreset() {
        auto& atu = emu_.Get<Iop13xxAtuState>();
        /* siemens_mp377_v1040 nk.exe OEMAddressTable at VA 80409F00;
           Intel 81341/81342 Developer's Manual 315037-002US, tables 27 and 63. */
        atu.SetAtucmd(Iop13xxAtuState::kAtucmdMemorySpaceEnable |
                      Iop13xxAtuState::kAtucmdBusMasterEnable);
        atu.SetAtucr(Iop13xxAtuState::kAtucrOutboundEnable);

        auto& regs = emu_.Get<SiemensSimaticMp377Sm501Regs>();
        const auto panel = emu_.Get<SiemensSimaticMp377Panel>().Descriptor();
        const uint32_t pitch = panel.width * (panel.bpp / 8u);

        /* siemens_mp377_v1040 nk.exe sub_80446E14;
           siemens_mp377_v1040 ddi_vgx.dll sub_29932DC and sub_2997CF8;
           SM501 Databook v1.02 section 5, Panel FB Offset and Panel FB Width. */
        const uint32_t units = pitch / 16u;
        /* siemens_mp377_v1040 ddi_vgx.dll sub_2994998;
           SM501 Databook v1.02 section 5, Panel Display Control. */
        Write(regs, kDcPanelControl, (1u << 2u) | 1u);
        Write(regs, kDcPanelFbAddress, 0u);
        Write(regs, kDcPanelFbOffset, (units << 20) | (units << 4));
        Write(regs, kDcPanelFbWidth, panel.width << 16);

        /* siemens_mp377_v1040 smibase.dll sub_2B544B4;
           SM501 Databook v1.02 sections 2 and 11. */
        Or(regs, kSm501PowerMode0GateReg, kSm501GateAc97I2sBit);
        Or(regs, kSm501PowerMode1GateReg, kSm501GateAc97I2sBit);
        Or(regs, kSm501Gpio31_0ControlReg, kSm501GpioAc97Mask);

        LOG(Board, "SiemensSimaticMp377BootloaderState: panel preset %ux%ux%u, pitch %u bytes\n",
            panel.width, panel.height, panel.bpp, pitch);
    }

    static void Write(SiemensSimaticMp377Sm501Regs& regs, uint32_t off, uint32_t v) {
        regs.WriteWord(kSm501RegsBarPa + off, v);
    }

    static void Or(SiemensSimaticMp377Sm501Regs& regs, uint32_t off, uint32_t bits) {
        regs.WriteWord(kSm501RegsBarPa + off, regs.ReadWord(kSm501RegsBarPa + off) | bits);
    }
};

} // namespace

REGISTER_SERVICE(SiemensSimaticMp377BootloaderState);

} // namespace siemens_mp377

}
