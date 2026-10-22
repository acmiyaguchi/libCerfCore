#include "../arm/arm_coproc_provider.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx28/imx28_id.h"
#include "../arm/arm_cp15_decisions.h"
#include "../arm/arm_cp15_registers.h"

namespace cerfcore {

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

class Arm926ejsCoprocProvider : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx28;
    }

    void OnReady() override {
        ArmCoprocProvider::OnReady();
        regs_ = &emu_.Get<ArmCp15Registers>();
    }

protected:
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        /* ARM DDI 0222B 6.8.2 p. 6-18: debug comms channel registers are "MRC p14, 0, Rd, c0, c0" etc. */
        if (a.cp_num == 14u) return {.action = A::kUnimplemented};
        if (a.cp_num != 15u) return {.action = A::kUndefined};

        /* ARM DDI 0198E Table 2-18 (p. 2-21) and "Test and clean operations"
           (p. 2-23): MRC c7,c10,3 and c7,c14,3 return bit 30 = 1 when the
           DCache holds no dirty lines. */
        if (a.read && a.opc1 == 0u && a.crn == 7u && a.opc2 == 3u &&
            (a.crm == 10u || a.crm == 14u)) {
            return {.action = A::kConstant, .constant = 0x40000000u};
        }

        /* ARM DDI 0198E section 2.3.13, Table 2-27 (p. 2-34). */
        if (a.opc1 == 0u && a.crn == 13u && a.crm == 0u && a.opc2 == 1u) {
            return {.action = A::kRegister, .reg = &regs_->File().context_id};
        }

        return Cp15().Decide(a);
    }

    ArmCoprocDecision DataTransfer(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }

    ArmCoprocDecision DataOperation(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }

private:
    ArmCp15Registers* regs_ = nullptr;
};

}

REGISTER_SERVICE_AS(Arm926ejsCoprocProvider, ArmCoprocProvider);

}
