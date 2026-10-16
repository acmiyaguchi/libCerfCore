#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_system_host.h>

namespace cerfcore {

class ArmCp15Decisions;

class ArmCoprocProvider : public Service {
public:
    using Service::Service;

    void OnReady() override;

    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& a);

    uint64_t& Accumulator() { return acc0_; }

protected:
    virtual cerfengine::ArmCoprocDecision RegisterTransfer(
        const cerfengine::ArmCoprocAccess& a) = 0;
    virtual cerfengine::ArmCoprocDecision DataTransfer(const cerfengine::ArmCoprocAccess& a) = 0;
    virtual cerfengine::ArmCoprocDecision DataOperation(
        const cerfengine::ArmCoprocAccess& a) = 0;
    virtual cerfengine::ArmCoprocDecision RegisterTransferDouble(
        const cerfengine::ArmCoprocAccess& a);

    const ArmCp15Decisions& Cp15() const { return *cp15_; }

    uint64_t acc0_ = 0;

private:
    ArmCp15Decisions* cp15_ = nullptr;
};

}
