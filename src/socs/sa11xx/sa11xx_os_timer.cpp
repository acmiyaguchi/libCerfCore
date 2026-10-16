#include "../intel_os_timer_impl.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "sa11xx_intc.h"

namespace {

/* SA-1110 §9.2.1.1 Table 9-1: IP26..29 = OSMR0..3 match. */
constexpr uint32_t kIntcOst0Bit = 26u;

/* SA-1110 §9.4.1: the OSCR "increments on rising edges of the 3.6864-MHz
   clock". */
constexpr uint32_t kOscrHz = 3686400u;

class Sa11xxOsTimer : public IntelOsTimerBase<kOscrHz> {
public:
    using IntelOsTimerBase<kOscrHz>::IntelOsTimerBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
    }

    uint32_t MmioBase() const override { return 0x90000000u; }

protected:
    void SetMatchLevel(uint32_t level4) override {
        emu_.Get<Sa11xxIntc>().SetSourceLevel(0xFu << kIntcOst0Bit,
                                              (level4 & 0xFu) << kIntcOst0Bit);
    }
};

REGISTER_SERVICE(Sa11xxOsTimer);

}  /* namespace */
