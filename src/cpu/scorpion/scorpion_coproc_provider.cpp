#include "../armv7a_coproc_provider_base.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/irq_controller.h"
#include "../../socs/msm8255/msm8255_id.h"

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

class ScorpionCoprocProvider : public Armv7aCoprocProviderBase {
public:
    using Armv7aCoprocProviderBase::Armv7aCoprocProviderBase;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

protected:
    /* ARM DDI 0406C.c Figure B3-35 (p. B3-1477); Cache and TCM lockdown
       registers, VMSA (p. B4-1753). */
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num == 15u && a.crn == 9u) {
            if (!a.read && a.opc1 == 0u && a.crm == 0u && a.opc2 == 6u) {
                return {.action = A::kNoOp};
            }
            return {.action = A::kUnimplemented};
        }
        if (a.cp_num == 15u && a.crn == 15u && a.opc1 == 7u && a.crm == 0u && a.opc2 == 3u) {
            if (!a.read) return {.action = A::kUnimplemented};
            return {.action  = A::kReadCallback,
                    .context = &emu_.Get<IrqController>(),
                    .read    = &IrqController::ReadPendingVectorHelper};
        }
        return Armv7aCoprocProviderBase::RegisterTransfer(a);
    }
};

}

REGISTER_SERVICE_AS(ScorpionCoprocProvider, ArmCoprocProvider);
