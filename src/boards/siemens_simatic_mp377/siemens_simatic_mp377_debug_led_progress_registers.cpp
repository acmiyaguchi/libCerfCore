#include "siemens_simatic_mp377_debug_led_peripheral.h"
#include "siemens_simatic_mp377_debug_leds.h"
#include <cerfcore/core/cerf_emulator.h>

#include <array>

namespace cerfcore {

namespace {
class SiemensSimaticMp377DebugLedProgressRegisters final : public SiemensSimaticMp377DebugLedPeripheral {
public:
    using SiemensSimaticMp377DebugLedPeripheral::SiemensSimaticMp377DebugLedPeripheral;

    uint32_t MmioBase() const override { return siemens_simatic_mp377::kDebugLedProgressBase; }
    uint32_t MmioSize() const override { return siemens_simatic_mp377::kDebugLedProgressEnd - MmioBase(); }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr) {
        case 0xF2FFFFFCu: values_[1] = value; return;
        case 0xF2FFFFFAu: values_[2] = value; return;
        case 0xF2FFFFF6u: values_[3] = value; return;
        default: SiemensSimaticMp377DebugLedPeripheral::WriteHalf(addr, value);
        }
    }

private:
    std::array<uint16_t, 4> values_{};
};
} // namespace
REGISTER_SERVICE(SiemensSimaticMp377DebugLedProgressRegisters);

}
