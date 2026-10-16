#include "vr41xx_icu.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* The ICU's second MMIO window, 0x0B000200 (VR4121 UM Table 15-1, VR4102 UM
   Table 14-1). */
class Vr41xxIcu2Mmio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.TryGet<Vr41xxIcu>() != nullptr; }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return emu_.Get<Vr41xxIcu>().Icu2Base(); }
    uint32_t MmioSize() const override { return emu_.Get<Vr41xxIcu>().Icu2Size(); }

    uint16_t ReadHalf(uint32_t addr) override {
        auto& icu = emu_.Get<Vr41xxIcu>();
        return icu.ReadHalf2(addr - icu.Icu2Base());
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        auto& icu = emu_.Get<Vr41xxIcu>();
        icu.WriteHalf2(addr - icu.Icu2Base(), value);
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr41xxIcu2Mmio);

}
