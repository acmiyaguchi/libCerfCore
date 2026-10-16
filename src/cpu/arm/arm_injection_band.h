#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_mmu_state.h>

namespace cerfcore {

class EmulatedMemory;

class ArmInjectionBand : public Service {
public:
    using Service::Service;

    void OnReady() override;
    bool ShouldRegister() override;

    void Set(uint32_t va_base, uint32_t pa_base, uint32_t size);

    uint8_t* Serve(uint32_t va, cerfengine::ArmMmuAccess access, uint32_t& served_pa);

private:
    EmulatedMemory* memory_ = nullptr;

    uint32_t va_base_ = 0;
    uint32_t pa_base_ = 0;
    uint32_t size_    = 0;
};

}
