#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "cerf_virt_keyboard_regs.h"

#include <atomic>
#include <cstdint>

namespace cerfcore {

class CerfVirtKeyboard : public Peripheral {
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

    void PushKey(uint8_t vk, bool key_up);

private:
    std::atomic<uint32_t> ring_[CerfVirt::kKbRingCount]{};
    std::atomic<uint32_t> write_seq_{0};
};

}
