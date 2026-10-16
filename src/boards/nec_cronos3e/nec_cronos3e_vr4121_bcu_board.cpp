#include "../../socs/vr4121/vr4121_bcu_board.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_bcu_regs.h"
#include "../../socs/vr4121/vr4121_bcu_regs.h"
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

#include <cstdint>

namespace {

constexpr uint32_t kBank0ChipBytes = 0x02000000u;
constexpr uint16_t kBootRamSize    = 0x3334u;

class NecCronos3eVr4121BcuBoard : public Vr4121BcuBoard {
public:
    using Vr4121BcuBoard::Vr4121BcuBoard;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    bool Sdram() const override { return true; }

    std::optional<Vr4121DramWiring> DramWiring() const override {
        return Vr4121DramWiring{ true, { kBank0ChipBytes, 0u, 0u, 0u } };
    }

    std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const override {
        return {
            { vr41xx_bcu::kOffCnt1, vr4121_bcu::kCnt1Dram64 | vr4121_bcu::kCnt1Rd64d },
            { vr4121_bcu::kOffRamSize, kBootRamSize },
        };
    }
};

}

REGISTER_SERVICE_AS(NecCronos3eVr4121BcuBoard, Vr4121BcuBoard);
