#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>

#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>

namespace {

class CerfVirtPalette : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<DeviceConfig>().guest_additions;
    }

    void OnReady() override {
        fb_ = &emu_.Get<CerfVirtFramebuffer>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override {
        return emu_.Get<BoardContext>().GuestAdditionsWindowBase()
             + CerfVirt::kPaletteOffset;
    }
    uint32_t MmioSize() const override { return CerfVirt::kPaletteSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t idx = (addr - MmioBase()) >> 2u;
        return fb_->PaletteEntry(idx);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t idx = (addr - MmioBase()) >> 2u;
        fb_->SetPaletteEntry(idx, value & 0x00FFFFFFu);
        fb_->MarkDirty();
    }

private:
    CerfVirtFramebuffer* fb_ = nullptr;
};

REGISTER_SERVICE(CerfVirtPalette);

}
