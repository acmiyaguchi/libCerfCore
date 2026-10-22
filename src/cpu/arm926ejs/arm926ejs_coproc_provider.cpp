#include "../arm/arm_coproc_provider.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../socs/imx28/imx28_id.h"
#include "../arm/arm_cp15_decisions.h"
#include "../arm/arm_cp15_registers.h"

namespace cerfcore {

namespace {

/* ARM DDI 0198E Figure 2-13: base [31:12], size [5:2] (0 = absent), enable [0]; MCIMX28RM Rev. 2
   section 1.2 lists caches and no TCM. Figure 2-14: victim [28:26], preserve [0]. */
constexpr uint32_t kTcmBaseEnable  = 0xFFFFF001u;
constexpr uint32_t kTcmEnable      = 1u << 0;
constexpr uint32_t kTlbLockdownMask = 0x1C000001u;
constexpr uint32_t kTlbPreserve    = 1u << 0;

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

        /* ARM DDI 0198E 2.3.10 Table 2-22, 2.3.11 Table 2-25, B.1.2. */
        if (a.opc1 == 0u && a.crn == 9u && a.crm == 1u && a.opc2 <= 1u) {
            if (a.read) return {.action = A::kRegister, .reg = &regs_->File().tcm_region[a.opc2]};
            return {.action = A::kWriteCallbackKeyed, .context = this, .key = a.opc2,
                    .write_keyed = &WriteTcmRegion};
        }
        if (a.opc1 == 0u && a.crn == 10u && a.crm == 0u && a.opc2 == 0u) {
            if (a.read) return {.action = A::kRegister, .reg = &regs_->File().tlb_lockdown};
            return {.action = A::kWriteCallback, .context = this, .write = &WriteTlbLockdown};
        }
        if (a.opc1 == 0u && a.crn == 15u && a.crm == 1u && a.opc2 == 0u) {
            return {.action = A::kRegister, .reg = &regs_->File().debug_test_address};
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
    static void CERFENGINE_FASTCALL WriteTcmRegion(void* ctx, uint32_t key, uint32_t value) {
        auto* self = static_cast<Arm926ejsCoprocProvider*>(ctx);
        if ((value & kTcmEnable) != 0u) {
            self->emu_.Get<Fatal>().Die("ARM926EJ-S: TCM region %u enable with no TCM (value %08X)",
                                        key, value);
        }
        self->regs_->File().tcm_region[key] = value & kTcmBaseEnable;
    }

    static void CERFENGINE_FASTCALL WriteTlbLockdown(void* ctx, uint32_t value) {
        auto* self = static_cast<Arm926ejsCoprocProvider*>(ctx);
        if ((value & kTlbPreserve) != 0u) {
            self->emu_.Get<Fatal>().Die("ARM926EJ-S: TLB lockdown preserve bit (value %08X)", value);
        }
        self->regs_->File().tlb_lockdown = value & kTlbLockdownMask;
    }

    ArmCp15Registers* regs_ = nullptr;
};

}

REGISTER_SERVICE_AS(Arm926ejsCoprocProvider, ArmCoprocProvider);

}
