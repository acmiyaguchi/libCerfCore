#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "vr4121_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* VR4121 DSU (Deadman's Switch Unit), UM Table 18-1: DSUCNTREG@0x0B0000E0 R/W,
   DSUSETREG@0xE2 R/W, DSUCLRREG@0xE4 W, DSUTIMREG@0xE6 R/W. */
constexpr uint32_t kBase = 0x0B0000E0u;
constexpr uint32_t kSize = 0x08u;

constexpr uint32_t kOffCntReg = 0x00u;

/* DSUCNTREG D0 DSWEN "Deadman's Switch function enable. 1: Enable, 0: Prohibit"; D15:1 are
   RFU, "Write 0 to these bits. 0 is returned after a read." Both reset rows are 0
   (UM 18.2.1). */
constexpr uint16_t kCntProhibit = 0x0000u;

class Vr4121Dsu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Vr4121;
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr - kBase != kOffCntReg) {
            Peripheral::WriteHalf(addr, value);
            return;
        }
        /* "The VR4121 automatically shuts down if 1 is not written to [DSUCLRREG] within the
           period specified in DSUSETREG" (UM 18.2.3). nk.exe writes DSUCNTREG = 0 at
           0x9F0B5BE0 and 0x9F0B8A9C; DSUSETREG/DSUCLRREG/DSUTIMREG have zero xrefs ROM-wide. */
        if (value != kCntProhibit) {
            HaltUnsupportedAccess("VR4121 DSU deadman's switch enabled", addr, value);
        }
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4121Dsu);
