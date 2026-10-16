#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

/* SIMpad SL4 keypad: 6 buttons on UCB1300 IO_DATA (reg 0) bits 0..5, ACTIVE-LOW
   (released=1, pressed=0). The CE4 PDD keyucb1x00.cpp polls + inverts them
   (keybddr.dll sub_13221BC: ~iodata & 0x3F); presenting them active-high makes
   every released button read pressed -> permanent VK_RETURN auto-repeat. */
class SiemensSimpadKeypad : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    /* Bit n set = button n (0..5) currently pressed. Written by the host-key
       adapter (UI thread), read by the UCB1300 codec (JIT thread). */
    void SetPressedMask(uint8_t mask) {
        pressed_.store(static_cast<uint8_t>(mask & 0x3Fu), std::memory_order_release);
    }

    /* UCB IO_DATA[5:0] as the PDD reads it: active-low (1=released, 0=pressed). */
    uint8_t ReleasedIoBits() const {
        return static_cast<uint8_t>(~pressed_.load(std::memory_order_acquire) & 0x3Fu);
    }

private:
    std::atomic<uint8_t> pressed_{0};
};

}
