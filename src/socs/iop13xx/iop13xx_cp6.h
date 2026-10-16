#pragma once

#include <cerfengine/arm/arm_system_host.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../irq_controller.h"

#include <cstdint>
#include <mutex>

namespace cerfengine { class ArmJit; }

namespace cerfcore {

class Iop13xxTimers;
class StateReader;
class StateWriter;

class Iop13xxCp6 : public IrqController, public ResetCauseLatch {
public:
    using IrqController::IrqController;

    bool ShouldRegister() override;
    void OnReady() override;

    void AssertIrq(int source_bit) override;
    void AssertSubIrq(int main_source_bit, int sub_source_bit) override;
    void DeAssertIrq(int source_bit) override;
    void SetSharedIrqLevel(int source_bit, uint32_t contributor_bit, bool asserted) override;
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestoreState();

    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& a);

    static uint32_t CERFENGINE_FASTCALL ReadHelper(void* self, uint32_t key);
    static void     CERFENGINE_FASTCALL WriteHelper(void* self, uint32_t key, uint32_t value);

    void LatchWarmReset() override;
    void LatchColdReset() override;
    void LatchWatchdogReset() override;

private:
    static constexpr uint32_t kResetCauseCoreTargeted = 1u << 4;
    static constexpr uint32_t kResetCauseWatchdog = 1u << 5;

    uint32_t ReadRegisterLocked(uint32_t key);
    void WriteRegisterLocked(uint32_t key, uint32_t value);
    uint32_t InterruptVectorLocked() const;
    bool HasPendingIrqLocked() const;
    void NotifyLocked();
    bool HasPendingFiqLocked() const;
    void ResetStateLocked();

    cerfengine::ArmJit*        jit_    = nullptr;
    Iop13xxTimers* timers_ = nullptr;
    mutable std::mutex state_mutex_;
    uint32_t intctl_[4]{};
    uint32_t intstr_[4]{};
    uint32_t pending_[4]{};
    uint32_t shared_irq_levels_[128]{};
    uint32_t intbase_ = 0;
    uint32_t intsize_ = 0;
    uint32_t rcsr_ = 0;
};

}
