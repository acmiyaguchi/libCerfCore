#pragma once

#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>
#include <vector>

struct Omap3530McBspDxWord {
    uint32_t start = 0;
    uint32_t bits  = 0;
    uint32_t value = 0;
};

struct Omap3530McBspDxFrame {
    uint64_t index     = 0;
    uint32_t period    = 0;
    bool     has_sync  = false;
    uint32_t sync_rise = 0;
    uint32_t sync_fall = 0;
    std::vector<Omap3530McBspDxWord> words;
};

class Omap3530McBspPeerListener {
public:
    virtual void OnPeerTimingWillChange(uint64_t now) = 0;
    virtual void OnPeerRestarted(uint64_t now)        = 0;
    virtual void OnPeerRescaled(uint64_t now)         = 0;

protected:
    ~Omap3530McBspPeerListener() = default;
};

class Omap3530McBspPeer {
public:
    virtual bool                  DrivesClockAndSync() const = 0;
    virtual GuestCycleClock::Rate BitRate() const = 0;
    virtual uint64_t              BitsAt(uint64_t cycle) const = 0;
    virtual uint64_t              CycleOfBit(uint64_t bit) const = 0;
    virtual uint32_t              FrameBits() const = 0;
    virtual uint32_t              SyncRise() const = 0;
    virtual uint32_t              SyncFall() const = 0;
    virtual void                  ReceiveFrame(const Omap3530McBspDxFrame& frame) = 0;
    virtual void                  FlushFrames() = 0;
    virtual uint32_t              DoutWord(const Omap3530McBspDxFrame& shape, uint32_t start, uint32_t bits) = 0;
    virtual void                  SetPeerListener(Omap3530McBspPeerListener* listener) = 0;

protected:
    ~Omap3530McBspPeer() = default;
};

class Omap3530McBspClockPad {
public:
    virtual GuestCycleClock::Rate PadRate() const = 0;
    virtual void                  RegisterPadListener(std::function<void()> fn) = 0;

protected:
    ~Omap3530McBspClockPad() = default;
};
