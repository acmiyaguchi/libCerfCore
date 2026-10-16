#pragma once

#include <cstdint>

#include <cerfcore/core/force_inline.h>
#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_mmu_state.h>
#include <cerfengine/arm/arm_system_host.h>

namespace cerfcore {

class ArmCp15Registers;
class ArmInjectionBand;
class ArmProcessorConfig;
class EmulatedMemory;
class PhysicalAddressMapper;

class ArmPageWalker : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override;

private:
    template <cerfengine::ArmMmuAccess kAccess>
    friend cerfengine::ArmTranslation CERFENGINE_FASTCALL cerfengine::host::ArmWalk(
        void* context, uint32_t mva, bool user, uint32_t control);
    friend bool CERFENGINE_FASTCALL cerfengine::host::ArmProbeWalk(
        void* context, uint32_t mva, uint32_t control, uint32_t* pa);

    template <cerfengine::ArmMmuAccess kAccess>
    CERF_FORCEINLINE cerfengine::ArmTranslation Walk(uint32_t p, bool is_user_mode,
                                                     uint32_t control_word);
    CERF_FORCEINLINE bool ProbeWalk(uint32_t mva, uint32_t control_word, uint32_t* pa);

    static bool MapPhysicalAddress(ArmPageWalker& self, uint64_t cpu_pa, uint32_t va,
                                   uint32_t domain, cerfengine::ArmMmuAccess access,
                                   uint32_t& system_pa);

    ArmCp15Registers*      cp15_             = nullptr;
    EmulatedMemory*        memory_           = nullptr;
    ArmProcessorConfig*    processor_config_ = nullptr;
    ArmInjectionBand*      injection_band_   = nullptr;
    PhysicalAddressMapper* address_mapper_   = nullptr;
};

}
