#pragma once
#include <cerfcore/core/service.h>
#include <cstdint>

class Imx51Gpu3dMemory : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    uint8_t* ReadSpan(uint64_t pa, uint64_t size, uint32_t mmu_config);
    uint8_t* WriteSpan(uint64_t pa, uint64_t size, uint32_t mmu_config);
    uint32_t ReadPa32(uint64_t pa, uint32_t mmu_config);
    void WritePa32(uint64_t pa, uint32_t value, uint32_t mmu_config);

private:
    uint8_t* MemorySpan(uint64_t pa, uint64_t size, bool write, uint32_t mmu_config);
};
