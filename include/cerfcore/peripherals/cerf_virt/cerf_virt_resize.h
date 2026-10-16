#pragma once

#include "../peripheral_base.h"

#include <atomic>
#include <cstdint>

class CerfVirtResize : public Peripheral {
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

    void RequestResize(uint32_t w, uint32_t h);

private:
    std::atomic<uint32_t> want_w_{0};
    std::atomic<uint32_t> want_h_{0};
    std::atomic<uint32_t> want_gen_{0};

    std::atomic<uint32_t> applied_w_{0};
    std::atomic<uint32_t> applied_h_{0};
    std::atomic<uint32_t> applied_gen_{0};
};
