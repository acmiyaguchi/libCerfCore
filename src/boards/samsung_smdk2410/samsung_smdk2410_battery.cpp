#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../host/battery_widget.h"
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x500FFFF0u;
constexpr uint32_t kSize = 0x00000004u;

constexpr uint32_t kRegIsOnBattery   = 0x00u;
constexpr uint32_t kRegChargePercent = 0x01u;
constexpr uint32_t kRegTemperature   = 0x02u;

class SamsungSmdk2410Battery : public Peripheral {
public:
    explicit SamsungSmdk2410Battery(CerfEmulator& e) : Peripheral(e) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<HostWidgetRegistry>().Register(&battery_);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t  ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;

private:
    BatteryWidget battery_;
    uint16_t      temperature_ = 0;   /* raw, not interpreted */
};

uint8_t SamsungSmdk2410Battery::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint8_t value = 0;
    const char* name = "?";
    switch (off) {
        case kRegIsOnBattery:
            value = battery_.IsOnBattery() ? 1u : 0u;
            name  = "IsOnBattery";
            break;
        case kRegChargePercent:
            value = static_cast<uint8_t>(100 - battery_.FillPercent());
            name  = "ChargePercent";
            break;
        default:
            HaltUnsupportedAccess("ReadByte", addr, 0);  /* noreturn */
    }
    LOG(Periph, "[Battery] read8 %s -> 0x%02X\n", name, value);
    return value;
}

uint16_t SamsungSmdk2410Battery::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (off != kRegTemperature) HaltUnsupportedAccess("ReadHalf", addr, 0);  /* noreturn */
    LOG(Periph, "[Battery] read16 Temperature -> 0x%04X\n", temperature_);
    return temperature_;
}

}  /* namespace */

REGISTER_SERVICE(SamsungSmdk2410Battery);

}
