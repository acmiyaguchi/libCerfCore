#pragma once

#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

class CerfEmulator;
class MediaQMq200Power;
class StateWriter;
class StateReader;

class MediaQMq200Gc {
public:
    static constexpr uint32_t kGc1 = 0u;
    static constexpr uint32_t kGc2 = 1u;

    struct ScanTiming {
        bool                  on      = false;
        bool                  running = false;
        GuestCycleClock::Rate rate;
        const char*           why     = nullptr;
        uint64_t              line    = 0;
        uint64_t              frame   = 0;
        uint64_t              vde     = 0;
    };

    struct Window {
        uint32_t depth        = 0;
        uint32_t x            = 0;
        uint32_t y            = 0;
        uint32_t w            = 0;
        uint32_t h            = 0;
        uint32_t start        = 0;
        int32_t  stride       = 0;
        uint32_t palette_base = 0;
    };

    static constexpr uint32_t kCtrl       = 0x00u;
    static constexpr uint32_t kHDisp      = 0x08u;
    static constexpr uint32_t kVDisp      = 0x0Cu;
    static constexpr uint32_t kVCount     = 0x1Cu;
    static constexpr uint32_t kHWin       = 0x20u;
    static constexpr uint32_t kVWin       = 0x24u;
    static constexpr uint32_t kStart      = 0x30u;
    static constexpr uint32_t kStride     = 0x38u;
    static constexpr uint32_t kCursorPos  = 0x40u;
    static constexpr uint32_t kCursorAddr = 0x44u;
    static constexpr uint32_t kCursorFg   = 0x48u;
    static constexpr uint32_t kCursorBg   = 0x4Cu;

    static constexpr uint32_t kCtrlEnable       = 1u << 0;
    static constexpr uint32_t kCtrlWindowEnable = 1u << 3;
    static constexpr uint32_t kCtrlCursorEnable = 1u << 8;

    static constexpr uint32_t kGc1VCount = 0xA01Cu;
    static constexpr uint32_t kGc2VCount = 0xA09Cu;

    explicit MediaQMq200Gc(CerfEmulator& emu) : emu_(emu) {}

    void PowerOnReset();

    static bool Owns(uint32_t roff);
    uint32_t Read(uint32_t roff) const;
    void     Write(uint32_t roff, uint32_t value);

    uint32_t Reg(uint32_t gc, uint32_t off) const { return regs_[gc][off / 4u]; }
    uint32_t Palette(uint32_t index) const { return palette_[index & 0xFFu]; }
    bool     Displays(uint32_t gc) const;
    Window   ActiveWindow(uint32_t gc, const MediaQMq200Power& power) const;
    static uint32_t DepthBits(uint32_t depth);
    static bool     DepthIndexed(uint32_t depth);
    static bool     DepthUsesPalette(uint32_t depth);
    uint32_t    DisplayWidth(uint32_t gc) const;
    uint32_t    DisplayHeight(uint32_t gc) const;
    ScanTiming  Gc1ScanTiming(const MediaQMq200Power& power) const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kRegsPerGc = 0x20u;

    CerfEmulator& emu_;

    uint32_t regs_[2][kRegsPerGc] = {};
    uint32_t palette_[256]        = {};
};
