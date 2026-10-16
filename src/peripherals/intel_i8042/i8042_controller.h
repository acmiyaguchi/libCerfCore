#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

#include <cerfcore/core/service.h>
#include "../ps2_keyboard/ps2_keyboard.h"
#include "../ps2_mouse/ps2_mouse.h"

class StateWriter;
class StateReader;

class I8042Controller : public Service {
public:
    explicit I8042Controller(CerfEmulator& emu);

    bool ShouldRegister() override;

    uint8_t ReadData();              /* port 0x60 read */
    uint8_t ReadStatus();            /* port 0x64 read */
    void    WriteData(uint8_t v);    /* port 0x60 write */
    void    WriteCommand(uint8_t v); /* port 0x64 write */

    /* The owning controller (M1535 southbridge) wires the keyboard IRQ1 / mouse
       IRQ12 lines here; an output byte for an interrupt-enabled channel pulses
       the matching sink (gated by cmd-byte bit0/bit1). */
    void SetKbdIrqSink(std::function<void()> fn) { kbd_irq_sink_ = std::move(fn); }
    void SetAuxIrqSink(std::function<void()> fn) { aux_irq_sink_ = std::move(fn); }

    /* Host input -> device stream (set-2 scancode bytes / 3-byte motion packet). */
    void HostKeyboardScancodes(const uint8_t* bytes, size_t n);
    void HostMouseMotion(int dx, int dy, uint32_t button_mask);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum class Pending { kNone, kWriteCmdByte, kAuxWrite };

    int  LoadOutputLocked();      /* 0=none, 1=kbd channel, 2=aux/mouse */
    void RaiseLoadedIrq(int which);
    void PumpAndRaise();

    std::mutex mtx_;
    uint8_t    cmd_byte_ = 0;
    Pending    pending_  = Pending::kNone;

    std::function<void()> kbd_irq_sink_;
    std::function<void()> aux_irq_sink_;

    uint8_t out_byte_ = 0;
    bool    out_full_ = false;
    bool    out_aux_  = false;
    std::deque<uint8_t> ctrl_resp_;

    Ps2Keyboard kbd_;
    Ps2Mouse    mouse_;
};
