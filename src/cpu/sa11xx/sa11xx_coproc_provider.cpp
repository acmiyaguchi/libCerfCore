#include "../arm/arm_coproc_provider.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa1100_id.h"
#include "../../socs/sa11xx/sa1110_id.h"
#include "../arm/arm_cp15_decisions.h"

namespace cerfcore {

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

class Sa11xxCoprocProvider : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

    bool ShouldRegister() override {
        const auto soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Sa1110 || soc == SocId::Sa1100;
    }

protected:
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num != 15u) return {.action = A::kUndefined};
        /* SA-1110 Dev Man §9.5.2.1: idle mode is entered by "mcr p15, 0, r0,
           c15, c2, 2" (disable clock switching), an uncached load, then
           "mcr p15, 0, r0, c15, c8, 2" - the wait for interrupt. */
        if (a.crn == 15u) {
            if (!a.read && a.crm == 8u && a.opc2 == 2u && a.opc1 == 0u) {
                return {.action = A::kWaitForInterrupt};
            }
            if (a.read) return {.action = A::kConstant, .constant = 0u};
            return {.action = A::kNoOp};
        }
        if (a.crn == 9u) {
            if (!a.read && a.opc1 == 0u &&
                ((a.opc2 == 0u && a.crm == 0u) ||
                 (a.opc2 == 1u && a.crm <= 3u) ||
                 (a.opc2 == 2u && a.crm <= 11u) ||
                 (a.opc2 == 4u && a.crm == 0u) ||
                 (a.opc2 == 5u && a.crm == 0u))) {
                return {.action = A::kNoOp};
            }
            return {.action = A::kUndefined};
        }
        if (a.crn == 14u) {
            if (a.read) return {.action = A::kConstant, .constant = 0u};
            return {.action = A::kNoOp};
        }
        return Cp15().Decide(a);
    }

    ArmCoprocDecision DataTransfer(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }

    ArmCoprocDecision DataOperation(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }
};

}

REGISTER_SERVICE_AS(Sa11xxCoprocProvider, ArmCoprocProvider);

}
