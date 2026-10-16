#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <cerfcore/boot/guest_cold_boot.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>

namespace {

constexpr uint32_t kBspArgsPa            = 0x30020000u;
constexpr uint32_t kScreenSignatureOff   = 0x44u;
constexpr uint32_t kBspScreenSignature   = 0xDE12DE34u;
constexpr uint16_t kDefaultColorDepthBpp = 16u;

class SamsungSmdk2410BspArgs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || bd->GetBoardFamilyId() != BoardFamilyId::SamsungSmdk2410) return false;
        return !emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        WriteArgs();
        emu_.Get<GuestColdBoot>().RegisterReplay([this] { WriteArgs(); });
    }

private:
    void WriteArgs() {
        auto& cfg = emu_.Get<DeviceConfig>();
        auto& mem = emu_.Get<EmulatedMemory>();

        const uint16_t w = static_cast<uint16_t>(cfg.board_configurable_screen_width);
        const uint16_t h = static_cast<uint16_t>(cfg.board_configurable_screen_height);
        const uint16_t bpp = cfg.board_configurable_screen_bpp != 0u
                           ? static_cast<uint16_t>(cfg.board_configurable_screen_bpp)
                           : kDefaultColorDepthBpp;

        const uint32_t base = kBspArgsPa + kScreenSignatureOff;
        mem.WriteWord(base + 0u, kBspScreenSignature);
        mem.WriteHalf(base + 4u, w);
        mem.WriteHalf(base + 6u, h);
        mem.WriteHalf(base + 8u, bpp);

        LOG(Board, "SamsungSmdk2410BspArgs: ScreenSignature=0x%08X "
                   "Width=%u Height=%u BitsPerPixel=%u at PA 0x%08X\n",
            kBspScreenSignature, (unsigned)w, (unsigned)h,
            (unsigned)bpp, base);
    }
};

}  /* namespace */

REGISTER_SERVICE(SamsungSmdk2410BspArgs);
