#include "../amd_command_set_flash.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/siemens_simatic_tp177b_4/siemens_simatic_tp177b_4_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>
#include <string>

namespace cerfcore {

/* Spansion S29GL256 (32 MB) NOR, P177 nGCS0 boot bank (PA 0); AMD command set
   per amdflash.c, ID 0x227E0001 (OAL accept gate sub_830465C0, common to the
   S29GL-P family). Density is the nGCS0 OAT decode; the OAL gives TFFS
   flashSize-15 MiB as FFS, and a chip under 17 MB zero-sizes partition 1 (vlbd.cpp:675). */

namespace {

constexpr uint32_t kBase        = 0x00000000u;  /* nGCS0 PA 0 */
constexpr uint32_t kSize        = 0x02000000u;  /* 32 MB */
constexpr uint32_t kIdent       = 0x227E0001u;
constexpr uint32_t kUnlockAddr1 = 0x555u << 1;  /* 0xAAA */
constexpr uint32_t kUnlockAddr2 = 0x2AAu << 1;  /* 0x554 */
constexpr uint32_t kSectorSize  = 0x10000u;     /* uniform 64 KB */

class SpansionS29Gl256 : public AmdCommandSetFlash {
public:
    using AmdCommandSetFlash::AmdCommandSetFlash;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    std::string Tooltip() const override { return "NOR Flash - Spansion S29GL256 (32 MB)"; }

protected:
    uint32_t AutoSelectIdent() const override { return kIdent; }
    uint32_t UnlockAddr1()     const override { return kUnlockAddr1; }
    uint32_t UnlockAddr2()     const override { return kUnlockAddr2; }
    uint32_t SectorSize(uint32_t /*io_addr*/) const override { return kSectorSize; }
};

}  /* namespace */

REGISTER_SERVICE(SpansionS29Gl256);

}
