#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <array>
#include <cstdint>

class Sa1110SdramDecode : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void ApplyMdcnfg(uint32_t mdcnfg);

private:
    static EmulatedMemory::AddressDecode Build(uint32_t rows, uint32_t columns,
                                               uint32_t drac, uint32_t native_drac);

    std::array<EmulatedMemory::AddressDecode, 7> decodes_{};
    const EmulatedMemory::AddressDecode* active_ = nullptr;
};
