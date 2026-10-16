#include "../arm/arm_coproc_provider.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx31/imx31_id.h"
#include "../arm/arm_cp15_decisions.h"

namespace cerfcore {

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

class Arm1136CoprocProvider : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx31;
    }

protected:
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        /* CP14 is the ARM1136 debug coprocessor (DDI 0211 Chapter 13:
           "CP14 registers reset" p. 13-43, "CP14 debug instructions"
           p. 13-44). */
        if (a.cp_num == 14u) return {.action = A::kUnimplemented};
        if (a.cp_num != 15u) return {.action = A::kUndefined};
        if (a.crn == 15u) {
            if (a.read) return {.action = A::kConstant, .constant = 0u};
            return {.action = A::kNoOp};
        }
        return Cp15().Decide(a);
    }

    /* DDI 0211I Table 13-28 (p. 13-44): "STC p14, c5, <addressing mode>"
       and "LDC p14, c5, <addressing mode>" are CP14 debug instructions,
       accessing the rDTR / wDTR. */
    ArmCoprocDecision DataTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num == 14u && a.crd == 5u) return {.action = A::kUnimplemented};
        return {.action = A::kUndefined};
    }

    ArmCoprocDecision DataOperation(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }
};

}

REGISTER_SERVICE_AS(Arm1136CoprocProvider, ArmCoprocProvider);

}
