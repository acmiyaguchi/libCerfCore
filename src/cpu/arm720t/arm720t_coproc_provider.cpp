#include "../arm/arm_coproc_provider.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../arm/arm_cp15_decisions.h"
#include "arm720t_id.h"

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

/* ARM720T TRM (DDI 0229C) §8.1.1 + Table 8-1: CP14 is the
   communications channel / debug controller coprocessor. */
ArmCoprocDecision Cp14OrUndefined(const ArmCoprocAccess& a) {
    if (a.cp_num == 14u) return {.action = A::kUnimplemented};
    return {.action = A::kUndefined};
}

class Arm720TCoprocProvider : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Arm720t;
    }

protected:
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num == 15u) return Cp15().Decide(a);
        return Cp14OrUndefined(a);
    }

    ArmCoprocDecision DataTransfer(const ArmCoprocAccess& a) override {
        return Cp14OrUndefined(a);
    }

    ArmCoprocDecision DataOperation(const ArmCoprocAccess& a) override {
        return Cp14OrUndefined(a);
    }
};

}

REGISTER_SERVICE_AS(Arm720TCoprocProvider, ArmCoprocProvider);
