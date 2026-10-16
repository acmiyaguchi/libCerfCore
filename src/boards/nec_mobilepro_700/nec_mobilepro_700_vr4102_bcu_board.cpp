#include "../../socs/vr4102/vr4102_bcu_board.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class NecMobilePro700Vr4102BcuBoard : public Vr4102BcuBoard {
public:
    using Vr4102BcuBoard::Vr4102BcuBoard;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    /* nec_mobilepro_700_ce2 nk.bin reset code 0xBFC00018..0xBFC00058 (file 0xC00018), the last
       store to each register before `jr 0xBF000000`. */
    std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const override {
        return {
            { 0x00u, 0xD500u },
            { 0x02u, 0x0000u },
            { 0x0Au, 0x1234u },
            { 0x0Eu, 0x0209u },
        };
    }
};

}

REGISTER_SERVICE_AS(NecMobilePro700Vr4102BcuBoard, Vr4102BcuBoard);

}
