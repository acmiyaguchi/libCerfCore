#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>
#include "cerf_virt_dma_arena.h"

#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>

namespace {

class CerfVirtArenaCtl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override {
        return emu_.Get<BoardContext>().GuestAdditionsWindowBase() +
               CerfVirt::kArenaCtlOffset;
    }
    uint32_t MmioSize() const override { return CerfVirt::kArenaCtlSize; }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == CerfVirt::kArenaCtlClaimPid) {
            emu_.Get<CerfVirtDmaArena>().Claim(value);
            return;
        }
        LOG(Cerf, "[CerfVirtArenaCtl] write 0x%08X to unmodeled offset 0x%X\n",
            value, off);
        CerfFatalExit();
    }
};

REGISTER_SERVICE(CerfVirtArenaCtl);

}
