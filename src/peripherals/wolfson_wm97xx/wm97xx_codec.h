#pragma once

#include "../ac97_codec.h"
#include "wm97xx_digitiser.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfengine { class HostRequestChannel; }

class Wm97xxCodec : public Ac97Codec {
public:
    using Ac97Codec::Ac97Codec;

    void OnReady() override;

    void SetPenPosition(uint16_t raw_x, uint16_t raw_y);
    void QueuePenEdge(bool down);
    void SetPenLineObserver(std::function<void(bool down)> fn) { pen_line_ = std::move(fn); }

    void     LinkStopped(uint64_t frame) override;
    void     LinkStarted(uint64_t frame) override;
    uint32_t DigitiserSlot() override;
    uint64_t SlotWordsBefore(uint32_t slot, uint64_t frames) override;
    bool     FrameOfSlotWord(uint32_t slot, uint64_t n, uint64_t& frame) override;
    uint16_t SlotWord(uint32_t slot, uint64_t n) override;
    void     PruneSlotWords(uint32_t slot, uint64_t frames) override;
    void     PostRestore() override;

protected:
    static constexpr uint16_t kAdcMax        = 0x0FFFu;
    static constexpr uint32_t kRegDigitiserSlot  = 0x76u;
    static constexpr uint32_t kRegDigitiserPower = 0x78u;
    static constexpr uint32_t kDelNone       = 15u;
    /* Cirrus Logic WM9713L Rev 4.0 page 76: a measurement takes "the delay time DEL, plus one AC-Link
       frame (20.8µs)". */
    static constexpr uint32_t kResultFrames  = 1u;
    /* Wolfson WM9705 page 45 Figure 22 note: for pressure "two data acquisition cycles are required";
       Cirrus Logic WM9713L Rev 4.0 page 76: "DEL plus two AC-Link frames". */
    static constexpr uint32_t kPressureFrames = 2u;

    static uint32_t FramesFor(bool pressure) { return pressure ? kPressureFrames : kResultFrames; }

    virtual uint16_t ConversionData(uint8_t tag) = 0;
    virtual void     Reconfigure(uint64_t frame, bool poll) = 0;
    virtual void     OnPenSignal(uint64_t, uint64_t) {}

    static uint32_t FastRateFrames(uint32_t cr);
    static uint8_t  RouteSlot(uint16_t d76);
    static bool     SlotMode(uint16_t d76);

    bool     DigitiserPowered();
    uint16_t ResultWord(const Wm97xxDigitiser::Result& r);
    uint16_t ReadResultRegister(uint64_t frame, uint16_t held);
    uint16_t RawX() const { return raw_x_.load(std::memory_order_relaxed); }
    uint16_t RawY() const { return raw_y_.load(std::memory_order_relaxed); }
    void     SavePen(StateWriter& w) const;
    void     RestorePen(StateReader& r);

    Wm97xxDigitiser digitiser_;
    bool            pen_down_ = false;

private:
    void SetPenDown(bool down);
    void DrainPenEdges();
    bool WakeOnPen(uint64_t now);

    std::atomic<uint16_t>     raw_x_{0};
    std::atomic<uint16_t>     raw_y_{0};
    std::function<void(bool)> pen_line_;
    cerfengine::HostRequestChannel*       host_requests_ = nullptr;
    std::mutex                edge_mtx_;
    std::vector<bool>         edges_;
};
