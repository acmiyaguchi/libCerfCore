#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pr31x00/pr31x00_io_pins.h"
#include "philips_nino_battery.h"
#include <cerfcore/boards/board_context.h>
#include "philips_nino_family_id.h"

#include <cstdint>

namespace {

/* IO[5] external power sense, active high. sib.dll sub_18D2F74 reads IO_CTL and takes
   ControlPanel\BackLight "ACTimeout"/"ACAuto" when the bit is set, "BatteryTimeout"/
   "BatteryAuto" when it is clear (sub_18D2E48); gwes.exe 0x00082C8C reads the same
   register and stores its AC flag from the same bit. */
constexpr uint32_t kAcPowerPresent = 1u << 5;

/* No other general purpose I/O pin has a reader: nk.exe's only IO_CTL access is the
   sub_9F411648 store, and across every ROM module the sole remaining non-$sp load at
   +0x180 belongs to sib.dll and gwes.exe, both testing bit 5. */

/* MFIO pin 25 is the charger status input: gwes.exe sub_83088 flags the main
   battery CHARGING only while on AC and this pin reads 0, so it must read high
   once the pack is full or the unit is on battery. */
constexpr uint32_t kMfioChargeStatus = 1u << 25;

class PhilipsNinoIoPins : public Pr31x00IoPins {
public:
    using Pr31x00IoPins::Pr31x00IoPins;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsNino;
    }

    uint32_t IoDin() const override {
        return emu_.Get<PhilipsNinoBattery>().IsOnBattery() ? 0u : kAcPowerPresent;
    }

    std::optional<uint32_t> MfioDin() const override {
        auto& batt = emu_.Get<PhilipsNinoBattery>();
        const bool charging = !batt.IsOnBattery() && batt.FillPercent() < 100;
        return charging ? 0u : kMfioChargeStatus;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsNinoIoPins, Pr31x00IoPins);
