#include "../intel_os_timer_impl.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "pxa255_id.h"
#include "../pxa2xx/pxa2xx_intc.h"

namespace {

/* PXA255 ICPR Table 4-35: IS26..29 = OS Timer match 0..3. */
constexpr uint32_t kIntcOst0Bit = 26u;

/* PXA255 Developer's Manual 278693 §4.4.2.4: the OSCR increments on rising
   edges of the 3.6864-MHz clock. */
constexpr uint32_t kOscrHz = 3686400u;

class Pxa255OsTimer : public IntelOsTimerBase<kOscrHz> {
public:
    using IntelOsTimerBase<kOscrHz>::IntelOsTimerBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa255;
    }

    uint32_t MmioBase() const override { return 0x40A00000u; }

protected:
    void SetMatchLevel(uint32_t level4) override {
        static_cast<Pxa2xxIntc&>(emu_.Get<IrqController>())
            .SetSourceLevel(0xFu << kIntcOst0Bit,
                            (level4 & 0xFu) << kIntcOst0Bit);
    }

    void OnResetLine() override {
        IntelOsTimerBase<kOscrHz>::OnResetLine();
        ResetRegistersToZero();
    }
};

REGISTER_SERVICE(Pxa255OsTimer);

}  /* namespace */
