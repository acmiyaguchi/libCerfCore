#include "../intel_os_timer_impl.h"

#include <cerfcore/boards/board_context.h>
#include "pxa270_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../pxa2xx/pxa2xx_intc.h"

namespace cerfcore {

namespace {

/* Intel PXA27x Developer's Manual 280000-001 Table 25-2 (page 25-5): IP[29]
   "OS timer equals Match register 3" .. IP[26] "OS timer equals Match
   register 0". */
constexpr uint32_t kIntcOst0Bit = 26u;

/* Intel PXA27x Developer's Manual 280000-001 §22.5.5: OSCR0 "is incremented on
   rising edges of the 3.25-MHz clock". */
constexpr uint32_t kOscrHz = 3250000u;

class Pxa27xOsTimer : public IntelOsTimerBase<kOscrHz> {
public:
    using IntelOsTimerBase<kOscrHz>::IntelOsTimerBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa270;
    }

    /* Table 22-12 (page 22-19) "OS Timers Register Summary": "0x40A0_0000
       OSMR0", "0x40A0_0010 OSCR0", "0x40A0_001C OIER". */
    uint32_t MmioBase() const override { return 0x40A00000u; }

    /* symbol_mk500 dump.bin OAL 0x801BFC38 builds 0xA6C00010, the uncached
       alias of PA 0x40A0_0010 OSCR0, then reads it as "LDRB r0,[r1]" /
       "LDRB r3,[r1,#1]" / "LDRB r2,[r1,#2]" / "LDRB r1,[r1,#3]" reassembled by
       "ORR r3,r0,r3,LSL#8" / "ORR r3,r3,r2,LSL#16" / "ORR r1,r3,r1,LSL#24".
       Intel PXA27x Developer's Manual 280000-001 Section 2.8 (page 2-9): "some
       units allow byte or half-word accesses. Refer to the unit chapter to
       determine what accesses are allowed"; Section 22.6 Table 22-12 (page
       22-19) states no access width for the OS timers. */
    uint8_t ReadByte(uint32_t addr) override {
        return static_cast<uint8_t>(LaneByte(addr));
    }

    FastReadFn FastReader() override { return &Pxa27xOsTimer::FastReadThunk; }

protected:
    void SetMatchLevel(uint32_t level4) override {
        static_cast<Pxa2xxIntc&>(emu_.Get<IrqController>())
            .SetSourceLevel(0xFu << kIntcOst0Bit,
                            (level4 & 0xFu) << kIntcOst0Bit);
    }

private:
    uint32_t LaneByte(uint32_t addr) {
        const uint32_t off = (addr - MmioBase()) & ~0x3u;
        if (off != 0x10u) HaltUnsupportedAccess("ReadByte", addr, 0);
        const uint32_t word  = FastRead(off, 4u);
        const uint32_t shift = (addr & 0x3u) * 8u;
        return (word >> shift) & 0xFFu;
    }

    static uint32_t FastReadThunk(void* ctx, uint32_t off, uint32_t width) {
        auto* self = static_cast<Pxa27xOsTimer*>(ctx);
        if (width == 4u) return self->FastRead(off, 4u);
        if (width == 1u) return self->LaneByte(self->MmioBase() + off);
        self->HaltUnsupportedAccess("FastRead", self->MmioBase() + off, 0);
    }
};

REGISTER_SERVICE(Pxa27xOsTimer);

}  /* namespace */

}
