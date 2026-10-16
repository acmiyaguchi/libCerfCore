#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x500F5000u;
constexpr uint32_t kSpan = 0x8u;

constexpr uint32_t kOffControl = 0x0u;
constexpr uint32_t kOffStatus  = 0x4u;

constexpr uint32_t kControlAttach = 0xFFFFFFFFu;
constexpr uint32_t kControlDetach = 0x00000000u;

class SamsungSmdk2410EmulatorServices : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSpan; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr - kBase == kOffStatus)
            return 0u;
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - kBase == kOffControl &&
            (value == kControlAttach || value == kControlDetach))
            return;
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
};

}

REGISTER_SERVICE(SamsungSmdk2410EmulatorServices);
