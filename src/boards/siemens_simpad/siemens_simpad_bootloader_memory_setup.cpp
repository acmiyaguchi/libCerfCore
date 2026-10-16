#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"
#include "../../socs/sa11xx/sa11xx_bootloader_memory_setup.h"
#include "siemens_simpad_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kOemMdcnfg      = 0x72547254u;
constexpr uint32_t kMdcnfgDe0      = 1u << 0;
constexpr uint32_t kMdcnfgDrac0    = 7u << 4;
constexpr uint32_t kRowBitsAtDrac0 = 9;

class SiemensSimpadBootloaderMemorySetup : public Sa11xxBootloaderMemorySetup {
public:
    using Sa11xxBootloaderMemorySetup::Sa11xxBootloaderMemorySetup;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    uint32_t Mdcnfg() const override {
        const auto& sdram = emu_.Get<Sa1110SdramGeometry>();
        const uint32_t drac = sdram.RowBits() + sdram.BankBits() - kRowBitsAtDrac0;
        return (kOemMdcnfg & ~kMdcnfgDrac0) | (drac << 4) | kMdcnfgDe0;
    }
};

}

REGISTER_SERVICE_AS(SiemensSimpadBootloaderMemorySetup, Sa11xxBootloaderMemorySetup);

}
