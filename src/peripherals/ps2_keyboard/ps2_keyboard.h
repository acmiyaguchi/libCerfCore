#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

class StateWriter;
class StateReader;

class Ps2Keyboard {
public:
    explicit Ps2Keyboard(std::function<void()> on_data)
        : on_data_(std::move(on_data)) {}

    /* Driver -> keyboard: a command byte (or a parameter for the previous
       command). Queues the ACK + any command-specific response. */
    void WriteCommand(uint8_t cmd);

    bool    HasData() const;   /* a response/scancode byte is pending */
    uint8_t ReadData();        /* keyboard -> driver, pops one byte */

    /* Host key -> set-2 scancode byte(s) (make = code, break = 0xF0+code, with a
       leading 0xE0/0xE1 for extended keys) + the data IRQ. Dropped while scanning
       is disabled (0xF5), matching a real PS/2 keyboard. */
    void QueueScancodes(const uint8_t* bytes, size_t n);

    /* reporting_/expect_param_ are guest-set modes that persist; the queued
       scancodes are transient host input and are cleared on restore. */
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

    /* Power-cycle reset: flush queued scancodes + return to defaults (scanning
       enabled), matching a keyboard after a system reset. */
    void Reset();

private:
    void PushLocked(uint8_t b) { out_.push_back(b); }
    void RaiseData();   /* invoke on_data_ (IRQ) outside the queue lock */

    mutable std::mutex    mtx_;
    std::deque<uint8_t>   out_;
    bool                  expect_param_ = false;
    bool                  reporting_    = true;   /* scanning on by default (PS/2 power-on) */
    std::function<void()> on_data_;
};
