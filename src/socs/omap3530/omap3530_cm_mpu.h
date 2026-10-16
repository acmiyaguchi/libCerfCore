#pragma once

#include "omap3530_prcm_stub_block.h"

#include <cstdint>

class Omap3530CmMpu : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004900u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    void OnReady() override;

    uint32_t ReadWord (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    void RestoreState(StateReader& r) override;
    void PostRestore() override { ApplyRate(); }

    uint64_t ArmFclkHz() const;

protected:
    const char* Label() const override { return "CM_MPU"; }
    const char* RegisterName(uint32_t off) const override;

private:
    void SeedBootDpllLocked();
    bool DeriveArmFclkLocked(uint64_t& hz, const char*& why) const;
    void ApplyRate();

    uint64_t osc_hz_ = 0;
};
