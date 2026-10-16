#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <deque>
#include <mutex>

/* HP Jornada 820 keyboard state: maps host VKs to the HP matrix scancodes
   (inverse of keybddr.dll's scancode->VK table @ 0x12C16F8), queues them for the
   SSP slave to clock out one-per-read, and signals each by a GPIO0 falling edge
   (INTC source 0 -> keyboard SYSINTR 32, keybddr sub_12C2604). */
class HpJornada820Keyboard : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* From the KeyboardInput adapter: a Win32 VK + up/down edge (host thread). */
    void OnHostKey(uint8_t vk, bool key_up);

    /* SSP slave dummy read (JIT thread): the next queued scancode (key-up =
       code | 0x80, keybddr sub_12C239C bit7), or 0 when none. */
    uint16_t NextScancode();

private:
    void PulseKbdIrqLine();

    std::mutex          mtx_;
    std::deque<uint8_t> pending_;
};
