#pragma once
#include <cerfcore/core/service.h>
#include <cstdint>
#include <unordered_map>

namespace cerfcore {

class Imx51Gpu3dBlit : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void Draw(uint32_t ctrl, uint64_t packet_address,
              const std::unordered_map<uint32_t, uint32_t>& registers, uint32_t mmu_config);

private:
    uint32_t BlitReg(const std::unordered_map<uint32_t, uint32_t>& registers, uint32_t index, uint64_t pa);
    static float AsFloat(uint32_t value);
    [[noreturn]] void HaltUnsupportedAccess(const char* op, uint32_t address, uint64_t value) const;
};

}
