#pragma once

#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_system_host.h>

namespace cerfcore {

class ArmCp15Registers;
class ArmProcessorConfig;

class ArmCp15Decisions : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& a) const;
    bool UserAllowed(const cerfengine::ArmCoprocAccess& a) const;

private:
    cerfengine::ArmCoprocDecision Identification(const cerfengine::ArmCoprocAccess& a) const;
    cerfengine::ArmCoprocDecision SystemControl(const cerfengine::ArmCoprocAccess& a) const;
    cerfengine::ArmCoprocDecision FaultRegister(const cerfengine::ArmCoprocAccess& a,
                                                uint32_t v6_opc2, uint32_t* v6_reg,
                                                uint32_t* legacy_reg) const;
    cerfengine::ArmCoprocDecision ProcessId(const cerfengine::ArmCoprocAccess& a) const;
    cerfengine::ArmCoprocDecision CacheOperation(const cerfengine::ArmCoprocAccess& a) const;
    cerfengine::ArmCoprocDecision CacheRead(const cerfengine::ArmCoprocAccess& a) const;
    cerfengine::ArmCoprocDecision TlbOperation(const cerfengine::ArmCoprocAccess& a) const;

    ArmCp15Registers*   regs_   = nullptr;
    ArmProcessorConfig* config_ = nullptr;
};

}
