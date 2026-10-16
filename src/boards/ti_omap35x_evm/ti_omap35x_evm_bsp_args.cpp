#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBspArgsPa        = 0x80000000u;
constexpr uint32_t kOalArgsSignature = 0x53475241u;   /* 'SGRA' */
constexpr uint16_t kOalArgsVersion   = 1;
constexpr uint16_t kBspArgsVersion   = 1;

constexpr uint32_t kOffSignature   = 0x00u;
constexpr uint32_t kOffOalVersion  = 0x04u;
constexpr uint32_t kOffBspVersion  = 0x06u;
constexpr uint32_t kOffUpdateMode  = 0x08u;
constexpr uint32_t kOffColdBoot    = 0x0Cu;
constexpr uint32_t kOffDeviceID    = 0x10u;
constexpr uint32_t kOffImageLaunch = 0x14u;
constexpr uint32_t kOffKitl        = 0x18u;
constexpr uint32_t kSizeKitl       = 0x28u;

class TiOmap35xEvmBspArgs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
    }

    void OnReady() override {
        WriteArgs();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteArgs(); });
    }

private:
    void WriteArgs() {
        auto& mem = emu_.Get<EmulatedMemory>();

        mem.WriteWord(kBspArgsPa + kOffSignature,   kOalArgsSignature);
        mem.WriteHalf(kBspArgsPa + kOffOalVersion,  kOalArgsVersion);
        mem.WriteHalf(kBspArgsPa + kOffBspVersion,  kBspArgsVersion);
        mem.WriteWord(kBspArgsPa + kOffUpdateMode,  0u);
        /* If coldBoot is 0, OALIoCtlHalQueryFormatPartition returns
           FALSE → FATFS skips formatting the blank PART_DOS32
           partition → BOOTFS_MOUNT_GUID never advertises. */
        mem.WriteWord(kBspArgsPa + kOffColdBoot,    1u);
        mem.WriteWord(kBspArgsPa + kOffDeviceID,    0u);
        mem.WriteWord(kBspArgsPa + kOffImageLaunch, 0u);
        for (uint32_t i = 0; i < kSizeKitl; i += 4) {
            mem.WriteWord(kBspArgsPa + kOffKitl + i, 0u);
        }

        LOG(Board, "TiOmap35xEvmBspArgs: BSP_ARGS at PA 0x%08X "
                   "signature=0x%08X coldBoot=1\n",
            kBspArgsPa, kOalArgsSignature);
    }
};

}  /* namespace */

REGISTER_SERVICE(TiOmap35xEvmBspArgs);

}
