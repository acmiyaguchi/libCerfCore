#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* nk.exe sub_9F40F39C polls CS2+$1024 with `lw` and treats any word OTHER than
   0xFFFF as a debug module being present (0x9F40F42C `li $t3, 0xFFFF`, 0x9F40F434
   `lw $a0, 0($t2)`). A retail Velo has none, so anything else here sets
   0x80009FC4 bit 5 and enables PPSH and the debug serial port. */
constexpr uint32_t kProbeReg = 0x10401024u;
constexpr uint32_t kAbsent   = 0x0000FFFFu;

class PhilipsVelo1DebugModule : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kProbeReg; }
    uint32_t MmioSize() const override { return 0x4u; }

    uint32_t ReadWord(uint32_t) override { return kAbsent; }
};

}  /* namespace */

REGISTER_SERVICE(PhilipsVelo1DebugModule);

}
