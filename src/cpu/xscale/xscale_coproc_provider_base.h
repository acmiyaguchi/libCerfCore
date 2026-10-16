#pragma once

#include <cstdint>

#include "../arm/arm_coproc_provider.h"
#include <cerfengine/calling_convention.h>

class XscaleCoprocProviderBase : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

protected:
    cerfengine::ArmCoprocDecision RegisterTransfer(const cerfengine::ArmCoprocAccess& a) override;
    cerfengine::ArmCoprocDecision DataTransfer(const cerfengine::ArmCoprocAccess& a) override;
    cerfengine::ArmCoprocDecision DataOperation(const cerfengine::ArmCoprocAccess& a) override;
    cerfengine::ArmCoprocDecision RegisterTransferDouble(
        const cerfengine::ArmCoprocAccess& a) override;

    virtual cerfengine::ArmCoprocDecision PwrmodeWrite() = 0;
    virtual cerfengine::ArmCoprocDecision UnhandledCoprocessor(
        const cerfengine::ArmCoprocAccess& a) = 0;
    virtual cerfengine::ArmCoprocDecision ClkcfgTransfer(const cerfengine::ArmCoprocAccess& a);

    cerfengine::ArmCoprocDecision PwrmodeRunIdleSleep();

private:
    static void CERFENGINE_FASTCALL EnterDeepSleepHook(void* self);
};
