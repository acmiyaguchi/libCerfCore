#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>
#include <vector>

/* Maps host VKs to Jornada MCU scancodes (inverse of TSCkbdr.dll's scancode->VK
   table word_FC10A0 @ 0xFC10A0) and signals them by a GPIO0 falling edge (HP doc
   §4.3). The MCU GetScanKeyCode (0x90) handler drains the queue. */
class HpJornada700Keyboard : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* From the KeyboardInput adapter: a normalized Win32 VK + up/down edge. */
    void OnHostKey(uint8_t vk, bool key_up);

    /* MCU GetScanKeyCode (0x90): append all pending scancodes (key-up = code
       | 0x80, HP doc §4.3) in arrival order and clear the queue. */
    void DrainScancodes(std::vector<uint8_t>& out);

private:
    void PulseKbdIrqLine();

    mutable std::mutex   mtx_;
    std::vector<uint8_t> pending_;
};
