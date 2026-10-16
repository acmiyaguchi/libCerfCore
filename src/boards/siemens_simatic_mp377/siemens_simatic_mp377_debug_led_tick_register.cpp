#include "siemens_simatic_mp377_debug_led_peripheral.h"
#include "siemens_simatic_mp377_debug_leds.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {
class SiemensSimaticMp377DebugLedTickRegister final : public SiemensSimaticMp377DebugLedPeripheral {
public:
    using SiemensSimaticMp377DebugLedPeripheral::SiemensSimaticMp377DebugLedPeripheral;

    uint32_t MmioBase() const override { return siemens_simatic_mp377::kDebugLedTickBase; }
    uint32_t MmioSize() const override { return siemens_simatic_mp377::kDebugLedTickEnd - MmioBase(); }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr == siemens_simatic_mp377::kDebugLedTickBase) {
            value_ = value;
            return;
        }
        SiemensSimaticMp377DebugLedPeripheral::WriteHalf(addr, value);
    }

    void SaveState(StateWriter& w) override { w.Write("value", value_); }
    void RestoreState(StateReader& r) override { r.Read("value", value_); }

private:
    uint16_t value_{};
};
} // namespace
REGISTER_SERVICE(SiemensSimaticMp377DebugLedTickRegister);

}
