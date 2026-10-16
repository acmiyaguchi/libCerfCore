#include "sa11xx_memory_register_map.h"

#include "sa11xx_boot_straps.h"
#include "sa1110_id.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

namespace {

constexpr uint32_t kMdcnfg  = 0x00u / 4u;
constexpr uint32_t kMdcas00 = 0x04u / 4u;
constexpr uint32_t kMsc0    = 0x10u / 4u;
constexpr uint32_t kMdrefr  = 0x1Cu / 4u;
constexpr uint32_t kSmcnfg  = 0x30u / 4u;

/* SA-1110 Developer's Manual §10.2.1-§10.2.4 and §10.3 Reset rows; §10.7: "On sleep
   reset, the memory pins and controller are in the same state as after hardware reset". */
class Sa1110MemoryRegisterMap : public Sa11xxMemoryRegisterMap {
public:
    using Sa11xxMemoryRegisterMap::Sa11xxMemoryRegisterMap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Sa1110;
    }

    uint32_t RegisterCount() const override { return 13; }

    void ApplyResetValues(uint32_t (&regs)[kMaxRegisters]) const override {
        const auto& straps = emu_.Get<Sa11xxBootStraps>();
        const uint32_t smrom = straps.SmromEnHigh() ? 1u : 0u;
        regs[kMdcnfg]  &= ~0x00030003u;
        regs[kMdcas00]  = (regs[kMdcas00] & ~0x00000FFFu) | 0x0000057Fu;
        regs[kMsc0]     = (regs[kMsc0] & 0xFFFF0000u) | 0x0000FFF8u |
                          (straps.RomSelHigh() ? 0u : 0x4u);
        regs[kMdrefr]   = (regs[kMdrefr] & ~0xB6770000u) | 0x84440000u |
                          (smrom ? 0x00030000u : 0u);
        regs[kSmcnfg]   = (regs[kSmcnfg] & ~0x0003F073u) | 0x0000C040u | smrom;
    }
};

}

REGISTER_SERVICE_AS(Sa1110MemoryRegisterMap, Sa11xxMemoryRegisterMap);
