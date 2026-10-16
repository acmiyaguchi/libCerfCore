#include "armv7a_coproc_provider_base.h"

#include "arm/arm_cp15_decisions.h"

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

ArmCoprocDecision Armv7aCoprocProviderBase::RegisterTransfer(const ArmCoprocAccess& a) {
    if (a.cp_num == 15u) return Cp15().Decide(a);
    /* ARM DDI 0406C.c C6.4 "The CP14 debug register interface"
       (p. C6-2123). */
    if (a.cp_num == 14u) return {.action = A::kUnimplemented};
    return {.action = A::kUndefined};
}

/* DDI 0406C.c B3.15.2 (p. B3-1446); C6.4 (p. C6-2124). */
ArmCoprocDecision Armv7aCoprocProviderBase::DataTransfer(const ArmCoprocAccess& a) {
    if (a.cp_num == 14u && a.crd == 5u) return {.action = A::kUnimplemented};
    return {.action = A::kUndefined};
}

ArmCoprocDecision Armv7aCoprocProviderBase::DataOperation(const ArmCoprocAccess&) {
    return {.action = A::kUndefined};
}
