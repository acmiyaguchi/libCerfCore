#pragma once

#include "arm/arm_coproc_provider.h"

namespace cerfcore {

class Armv7aCoprocProviderBase : public ArmCoprocProvider {
public:
    using ArmCoprocProvider::ArmCoprocProvider;

protected:
    cerfengine::ArmCoprocDecision RegisterTransfer(const cerfengine::ArmCoprocAccess& a) override;
    cerfengine::ArmCoprocDecision DataTransfer(const cerfengine::ArmCoprocAccess& a) override;
    cerfengine::ArmCoprocDecision DataOperation(const cerfengine::ArmCoprocAccess& a) override;
};

}
