#include "xscale_coproc_provider_base.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../arm/arm_cp15_decisions.h"

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;
using C = cerfengine::ArmCoprocCase;

ArmCoprocDecision XscaleCoprocProviderBase::RegisterTransfer(const ArmCoprocAccess& a) {
    if (a.cp_num == 15u) {
        if (a.crn == 15u && a.crm == 1u && a.opc2 == 0u && a.opc1 == 0u) {
            return {.action = A::kCoprocessorAccess};
        }
        if (a.crn == 14u) {
            const bool is_dbg = a.opc1 == 0u && a.opc2 == 0u &&
                (a.crm == 0u || a.crm == 3u || a.crm == 4u || a.crm == 8u || a.crm == 9u);
            if (!is_dbg) return {.action = A::kUnimplemented};
            if (a.read) return {.action = A::kConstant, .constant = 0u};
            return {.action = A::kNoOp};
        }
        /* XScale Core Dev Manual Table 7-3 (page 80): register 1 is CRm=0 only;
           Table 7-1 (page 78): CRm "should be programmed to zero ... unless a
           value has been specified in the command". */
        if (a.crn == 1u && a.opc1 == 0u && a.crm != 0u) {
            if (a.crm != 1u || a.opc2 != 0u) return {.action = A::kUnimplemented};
            if (a.read) return {.action = A::kConstant, .constant = 0u};
            return {.action = A::kNoOp};
        }
        if (a.crn == 7u && a.crm == 2u && a.opc2 == 5u && a.opc1 == 0u) {
            return {.action = A::kNoOp};
        }
        return Cp15().Decide(a);
    }
    if (a.cp_num == 14u && a.opc1 == 0u && a.crm == 0u && a.opc2 == 0u) {
        if (a.crn == 7u) {
            if (a.read) return {.action = A::kConstant, .constant = 0u};
            return PwrmodeWrite();
        }
        /* XScale Core Dev Manual Table 7-25: CCLKCFG "is defined by the
           ASSP". */
        if (a.crn == 6u) return ClkcfgTransfer(a);
        if (a.read) return {.action = A::kConstant, .constant = 0u};
        return {.action = A::kNoOp};
    }
    return UnhandledCoprocessor(a);
}

ArmCoprocDecision XscaleCoprocProviderBase::ClkcfgTransfer(const ArmCoprocAccess&) {
    return {.action = A::kUnimplemented};
}

ArmCoprocDecision XscaleCoprocProviderBase::DataTransfer(const ArmCoprocAccess& a) {
    return UnhandledCoprocessor(a);
}

ArmCoprocDecision XscaleCoprocProviderBase::DataOperation(const ArmCoprocAccess& a) {
    return UnhandledCoprocessor(a);
}

/* MCRR/MRRC - XScale supports these only to CP0, the DSP 40-bit
   accumulator acc0 (MAR/MRA, Core Dev Manual §2.3.1 Table 2-6); any
   other coprocessor UNDs (§2.2.4). */
ArmCoprocDecision XscaleCoprocProviderBase::RegisterTransferDouble(const ArmCoprocAccess& a) {
    if (a.cp_num != 0u) return {.action = A::kUndefined};
    return {.action    = A::kRegister40,
            .gate_mask = 1u,
            .reg       = reinterpret_cast<uint32_t*>(&acc0_)};
}

ArmCoprocDecision XscaleCoprocProviderBase::PwrmodeRunIdleSleep() {
    return {.action     = A::kWriteValueSwitch,
            .context    = this,
            .hook       = &XscaleCoprocProviderBase::EnterDeepSleepHook,
            .case_count = 3u,
            .cases      = {{1u, C::kWaitForInterrupt}, {3u, C::kHook}, {0u, C::kContinue}}};
}

void CERFENGINE_FASTCALL XscaleCoprocProviderBase::EnterDeepSleepHook(void* self) {
    static_cast<XscaleCoprocProviderBase*>(self)->emu_.Get<GuestDeepSleep>().Enter();
}
