#include "../arm/arm_instruction_timing_model.h"

#include <bit>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/arm/decoded_insn.h>
#include <cerfengine/arm/place_fns.h>
#include "../../socs/sa11xx/sa1100_id.h"
#include "../../socs/sa11xx/sa1110_id.h"

namespace cerfcore {

namespace {

class Sa11xxInstructionTiming : public ArmInstructionTimingModel {
public:
    using ArmInstructionTimingModel::ArmInstructionTimingModel;

    bool ShouldRegister() override {
        const auto soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Sa1100 || soc == SocId::Sa1110;
    }

    /* SA-1110 Dev Man §4.2 Table 4-1 issue cycles; the control field is mask<0>
       (DDI 0406C A8.8.112). */
    uint16_t ArmCycleCost(const cerfengine::DecodedInsn& d) const override {
        uint16_t cost = 1;
        if (d.place_fn == &cerfengine::PlaceBlockDataTransfer) {
            const unsigned n = static_cast<unsigned>(std::popcount(d.register_list));
            cost = static_cast<uint16_t>(n < 2 ? 2 : n);
        } else if (d.place_fn == &cerfengine::PlaceMRSorMSR) {
            cost = (d.s != 0u && (d.crn & 0x1u) != 0u) ? 3 : 1;
        } else if (d.place_fn == &cerfengine::PlaceMSRImmediate) {
            cost = (d.crn & 0x1u) != 0u ? 3 : 1;
        } else if (d.place_fn == &cerfengine::PlaceMultiply) {
            cost = d.op1 >= 4u ? 2 : 1;
        } else if (d.place_fn == &cerfengine::PlaceLoadStoreExtension) {
            cost = d.op1 == 0u ? 2 : 1;
        }
        return cost;
    }
};

}

REGISTER_SERVICE_AS(Sa11xxInstructionTiming, ArmInstructionTimingModel);

}
