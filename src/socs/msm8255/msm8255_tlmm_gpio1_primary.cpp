#include "msm8255_hw_revision.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0xABC00000u;
constexpr uint32_t kSize = 0x00002000u;

class Msm8255TlmmGpio1Primary : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        uint32_t value = 0;
        if (cerf_msm8255_hw_revision::Read(addr - kBase, value)) return value;
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
};

}  // namespace

REGISTER_SERVICE(Msm8255TlmmGpio1Primary);
