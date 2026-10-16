#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfengine/calling_convention.h>
#include "../../socs/cycle_anchored_counter.h"

namespace cerfengine { struct MipsCpuState; }

class MipsCoreClock;
class MipsProcessorConfig;
class MipsTlbModel;

namespace cerfengine { class MipsTlbContext; }

class MipsCp0Ops : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override;

    void OnCpuReset();
    void SaveCount();
    void OnCpuStateRestored();

    uint32_t CountNow();

    static uint32_t CERFENGINE_FASTCALL ReadRandom(void* ops);
    static uint32_t CERFENGINE_FASTCALL ReadCount(void* ops);
    static void     CERFENGINE_FASTCALL WriteCount(void* ops, uint32_t value);
    static void     CERFENGINE_FASTCALL WriteCompare(void* ops, uint32_t value);
    static void     CERFENGINE_FASTCALL WriteEntryHiR3000(void* ops, uint32_t value);
    static void     CERFENGINE_FASTCALL WriteEntryHiR4000(void* ops, uint32_t value);

private:
    void AnchorSavedCount();
    void SetCountRatio();
    void ArmCompare(uint64_t now);
    void OnCompareMatch();

    cerfengine::MipsCpuState*               cpu_state_     = nullptr;
    cerfengine::MipsTlbContext* tlb_context_   = nullptr;
    MipsTlbModel*               tlb_           = nullptr;
    MipsProcessorConfig*        config_        = nullptr;
    GuestCycleClock*            clock_         = nullptr;
    MipsCoreClock*              core_clock_    = nullptr;
    GuestCycleClock::Event*     compare_event_ = nullptr;
    NarrowCycleAnchoredCounter  count_;
};
