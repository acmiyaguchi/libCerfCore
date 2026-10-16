#include "../arm/arm_coproc_provider.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/s3c2410/s3c2410_id.h"
#include "../arm/arm_cp15_decisions.h"

namespace cerfcore {

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

class Arm920TCoprocProvider : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::S3c2410;
    }

protected:
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num == 15u) return Cp15().Decide(a);
        /* S3C2410A UM p. 2-1: ARM920T CP14 is the debug communications
           channel, "accessible with MCR and MRC instructions". */
        if (a.cp_num == 14u) return {.action = A::kUnimplemented};
        return {.action = A::kUndefined};
    }

    ArmCoprocDecision DataTransfer(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }

    ArmCoprocDecision DataOperation(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }
};

}

REGISTER_SERVICE_AS(Arm920TCoprocProvider, ArmCoprocProvider);

}
