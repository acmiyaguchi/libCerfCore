#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "omap3530_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include "omap3530_gpmc.h"

#include <cstdint>

namespace {

class Omap3530GpmcCs0Window : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Omap3530;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x08000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint16_t ReadHalf(uint32_t /*addr*/) override {
        return emu_.Get<Omap3530Gpmc>().DrainPrefetchByte16(0);
    }
    uint8_t ReadByte(uint32_t /*addr*/) override {
        return static_cast<uint8_t>(
            emu_.Get<Omap3530Gpmc>().DrainPrefetchByte16(0));
    }
    uint32_t ReadWord(uint32_t /*addr*/) override {
        const uint16_t lo = emu_.Get<Omap3530Gpmc>().DrainPrefetchByte16(0);
        const uint16_t hi = emu_.Get<Omap3530Gpmc>().DrainPrefetchByte16(0);
        return static_cast<uint32_t>(lo) |
               (static_cast<uint32_t>(hi) << 16);
    }

    void WriteByte(uint32_t /*addr*/, uint8_t value) override {
        emu_.Get<Omap3530Gpmc>().PushPrefetchByte8(0, value);
    }
    void WriteHalf(uint32_t /*addr*/, uint16_t value) override {
        emu_.Get<Omap3530Gpmc>().PushPrefetchByte16(0, value);
    }
    void WriteWord(uint32_t /*addr*/, uint32_t value) override {
        emu_.Get<Omap3530Gpmc>().PushPrefetchByte16(0,
            static_cast<uint16_t>(value & 0xFFFFu));
        emu_.Get<Omap3530Gpmc>().PushPrefetchByte16(0,
            static_cast<uint16_t>(value >> 16));
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530GpmcCs0Window);
