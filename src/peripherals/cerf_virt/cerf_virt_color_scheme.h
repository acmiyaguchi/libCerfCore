#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/peripherals/cerf_virt/cerf_virt_addr_map.h>

#include <array>
#include <cstdint>

class CerfVirtColorScheme : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void ApplyFromConfig();

    std::array<uint32_t, CerfVirt::kColorSchemeMax> entries_{};
    uint32_t count_ = 0;
    bool present_ = false;
};
