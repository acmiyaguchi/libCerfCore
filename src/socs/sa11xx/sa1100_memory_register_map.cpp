#include "sa11xx_memory_register_map.h"

#include "sa11xx_boot_straps.h"
#include "sa1100_id.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

namespace {

constexpr uint32_t kMdcnfg = 0x00u / 4u;
constexpr uint32_t kMsc0   = 0x10u / 4u;

/* SA-1100 Technical Reference Manual Table 10-2, §10.2.1 and §10.2.3 Reset rows, and
   §10.7.1: power-on reset and sleep exit disable DE3:0 and set MSC0:SMCNFG0. */
class Sa1100MemoryRegisterMap : public Sa11xxMemoryRegisterMap {
public:
    using Sa11xxMemoryRegisterMap::Sa11xxMemoryRegisterMap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Sa1100;
    }

    uint32_t RegisterCount() const override { return 7; }

    void ApplyResetValues(uint32_t (&regs)[kMaxRegisters]) const override {
        regs[kMdcnfg] &= ~0x0000000Fu;
        regs[kMsc0]    = (regs[kMsc0] & 0xFFFF0000u) | 0x0000FFF8u |
                         (emu_.Get<Sa11xxBootStraps>().RomSelHigh() ? 0u : 0x4u);
    }
};

}

REGISTER_SERVICE_AS(Sa1100MemoryRegisterMap, Sa11xxMemoryRegisterMap);
