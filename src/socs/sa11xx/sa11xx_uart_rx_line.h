#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../rated_tick_count.h"
#include "sa11xx_uart_regs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace cerfcore {

class Sa11xxUartBase;
class StateReader;
class StateWriter;

class Sa11xxUartRxLine : public Service {
public:
    using Control = sa11xx_uart::Control;

    static constexpr uint16_t kBreakBegin = 1u << 8;
    static constexpr uint16_t kBreakEnd   = 1u << 9;
    static constexpr uint64_t kNever      = UINT64_MAX;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(Sa11xxUartBase* owner, const char* name);
    bool     Push(uint32_t port, const uint8_t* data, size_t n);
    void     Accept(uint32_t port, const Control& ctl, uint64_t now);
    void     Receive(uint32_t port, const Control& ctl, const uint8_t* data, size_t n,
                     uint64_t now);
    void     Loop(uint32_t port, uint8_t byte, const Control& ctl, uint64_t now);
    bool     Break(uint32_t port, bool low, const Control& ctl, uint64_t now);
    bool     Runs(uint32_t port, const Control& ctl) const;
    void     Stop(uint32_t port);
    void     Reset(uint32_t port);
    void     Rescale(uint32_t port, uint64_t now);
    bool     Retime(uint32_t port, const Control& old_ctl, const Control& ctl, uint64_t now);

    bool FrontDue(uint32_t port, uint64_t now) const {
        const Line& l = *line_[port];
        if (l.next_due == kNever) return false;
        return l.next_due == 0u || (l.timed && l.next_due <= l.half.TicksAt(now));
    }
    uint16_t Front(uint32_t port) const { return line_[port]->entries.front(); }
    void     PopFront(uint32_t port, uint64_t now);
    /* §11.11.1.3 (printed 11-110): "If the receive FIFO contains valid data and three frame periods
       elapse without the reception of data on RXD3, the receiver idle interrupt is generated." */
    bool Quiet(uint32_t port, const Control& ctl, uint64_t now) {
        Line& l = *line_[port];
        if (!l.entries.empty() || !l.timed) return false;
        if (!l.quiet && l.half.TicksAt(now) >= l.last_land + 6u * sa11xx_uart::FrameBits(ctl.utcr0)) {
            l.quiet = true;
        }
        return l.quiet;
    }
    uint64_t DueTick(uint32_t port, size_t index) const { return line_[port]->due[index]; }
    uint64_t QuietTick(uint32_t port, const Control& ctl) const {
        const Line& l = *line_[port];
        if (!l.timed) return kNever;
        if (!l.entries.empty()) return l.due.back() + 6u * sa11xx_uart::FrameBits(ctl.utcr0);
        return l.quiet ? kNever : l.last_land + 6u * sa11xx_uart::FrameBits(ctl.utcr0);
    }
    size_t   Pending(uint32_t port) const { return line_[port]->entries.size(); }
    uint64_t CycleOfTick(uint32_t port, uint64_t tick) const {
        return line_[port]->half.CycleOfTick(tick);
    }

    void Save(uint32_t port, StateWriter& w);
    void Restore(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 3u;

    struct Line {
        Sa11xxUartBase*       owner = nullptr;
        const char*           name  = "";
        std::deque<uint16_t>  entries;
        std::deque<uint64_t>  due;
        uint64_t              next_due = kNever;
        NarrowRatedTickCount  half;
        GuestCycleClock::Rate half_rate{};
        bool                  timed      = false;
        uint64_t              next_start = 0;
        uint64_t              last_land  = 0;
        bool                  quiet      = true;
        std::mutex            wire_mtx;
        std::vector<uint8_t>  wire;
    };

    void StartClock(Line& l, const Control& ctl, uint64_t now);
    static void Append(Line& l, uint16_t entry, uint64_t due);
    static void ClearEntries(Line& l);
    static bool Reframe(Line& l, const Control& old_ctl, const Control& ctl);
    static void LogNoClock(const Line& l, size_t n);
    static void LogLoopback(const Line& l, size_t n);

    GuestCycleClock* clock_ = nullptr;
    std::array<std::unique_ptr<Line>, kPorts> line_{};
    uint32_t count_ = 0;
};

}
