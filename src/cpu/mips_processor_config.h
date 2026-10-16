#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/engine_setup.h>
#include <cerfengine/mips/mips_cpu_state.h>

class MipsProcessorConfig : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t     Prid()         const { return prid_; }
    uint32_t     TlbSize()      const { return tlb_size_; }
    uint32_t     MinPageShift() const { return min_page_shift_; }
    uint32_t     PhysAddrMask() const { return phys_addr_mask_; }
    cerfengine::MipsIsaLevel IsaLevel() const { return isa_level_; }
    uint32_t     DeviceIpMask() const { return device_ip_mask_; }

    bool HasFpu()              const { return fpu_; }
    bool HasLlsc()             const { return llsc_; }
    bool HasCounter()          const { return counter_; }
    bool HasWatch()            const { return watch_; }
    bool HasVr41xxPowerModes() const { return vr41xx_power_modes_; }
    bool HasMips16()           const { return mips16_; }
    bool HasTx39MultiplyAdd()  const { return tx39_multiply_add_; }

    bool HasCp0Reg(uint32_t rd) const {
        if (rd == cerfengine::MipsCp0::kCount   || rd == cerfengine::MipsCp0::kCompare) return HasCounter();
        if (rd == cerfengine::MipsCp0::kWatchLo || rd == cerfengine::MipsCp0::kWatchHi) return HasWatch();
        return true;
    }

private:
    uint32_t     prid_           = 0;
    uint32_t     tlb_size_       = 0;
    uint32_t     min_page_shift_ = 0;
    uint32_t     phys_addr_mask_ = 0;
    uint32_t     device_ip_mask_ = 0;
    cerfengine::MipsIsaLevel isa_level_ = cerfengine::MipsIsaLevel::kMips1;

    bool fpu_                = false;
    bool llsc_               = false;
    bool counter_            = false;
    bool watch_              = false;
    bool vr41xx_power_modes_ = false;
    bool mips16_             = false;
    bool tx39_multiply_add_  = false;
};
