#define NOMINMAX

#include "sa11xx_uart_rx_line.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include "sa11xx_uart_base.h"

#include <algorithm>

using namespace sa11xx_uart;

namespace {

constexpr size_t kWireMax = 64u * 1024u;

}

bool Sa11xxUartRxLine::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxUartRxLine::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
}

uint32_t Sa11xxUartRxLine::Attach(Sa11xxUartBase* owner, const char* name) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Sa11xxUartRxLine: a fourth UART attached");
    const uint32_t index = count_++;
    line_[index] = std::make_unique<Line>();
    line_[index]->owner = owner;
    line_[index]->name  = name;
    return index;
}

bool Sa11xxUartRxLine::Runs(uint32_t port, const Control& ctl) const {
    GuestCycleClock::Rate bit{};
    return line_[port]->owner->BitClock(ctl, bit);
}

bool Sa11xxUartRxLine::Push(uint32_t port, const uint8_t* data, size_t n) {
    Line& l = *line_[port];
    std::lock_guard<std::mutex> lk(l.wire_mtx);
    if (l.wire.size() + n > kWireMax) {
        LOG(Caution, "[UART] %s RX: the guest is not taking received bytes, dropping %zu\n",
            l.name, n);
        return false;
    }
    l.wire.insert(l.wire.end(), data, data + n);
    return true;
}

/* §11.11.1.2 (printed 11-110): "The receive baud clock is synchronized with the data stream using a
   digital PLL each time the start bit is detected"; "Receive data is then sampled halfway through
   each bit period". */
void Sa11xxUartRxLine::StartClock(Line& l, const Control& ctl, uint64_t now) {
    if (l.timed && (!l.entries.empty() || l.next_start > l.half.TicksAt(now))) return;
    GuestCycleClock::Rate bit{};
    l.owner->BitClock(ctl, bit);
    l.half_rate = bit.Scaled(2u, 1u);
    if (!l.half.SetRate(clock_->ClockRate(), l.half_rate)) {
        emu_.Get<Fatal>().Die("%s: baud rate overflows the cycle clock ratio", l.name);
    }
    l.half.Start(now);
    l.timed      = true;
    l.next_start = 0u;
    l.last_land  = 0u;
}

void Sa11xxUartRxLine::Accept(uint32_t port, const Control& ctl, uint64_t now) {
    Line& l = *line_[port];
    std::vector<uint8_t> bytes;
    {
        std::lock_guard<std::mutex> lk(l.wire_mtx);
        bytes.swap(l.wire);
    }
    if (bytes.empty()) return;
    Receive(port, ctl, bytes.data(), bytes.size(), now);
}

/* §11.11.5.6 (printed 11-117): with LBM=1 "control of the TXD3 and RXD3 pins is given to the
   peripheral pin control (PPC) unit"; §11.11.1.1 (printed 11-110): "the receiver only tests for one
   stop bit per frame". */
void Sa11xxUartRxLine::Receive(uint32_t port, const Control& ctl, const uint8_t* data, size_t n,
                               uint64_t now) {
    Line& l = *line_[port];
    if ((ctl.utcr3 & kUtcr3Rxe) == 0u) return;
    if ((ctl.utcr3 & kUtcr3Lbm) != 0u) {
        LogLoopback(l, n);
        return;
    }
    if (!Runs(port, ctl)) {
        LogNoClock(l, n);
        return;
    }
    if (l.entries.size() + n > kWireMax) {
        LOG(Caution, "[UART] %s RX: the guest is not taking received bytes, dropping %zu\n",
            l.name, n);
        return;
    }
    StartClock(l, ctl, now);
    const uint64_t f   = FrameBits(ctl.utcr0);
    const uint64_t one = f - ((ctl.utcr0 & kUtcr0Sbs) != 0u ? 1u : 0u);
    uint64_t start = std::max(l.next_start, l.half.TicksAt(now));
    for (size_t i = 0; i < n; ++i) {
        Append(l, data[i], start + 2u * one - 1u);
        start += 2u * f;
    }
    l.next_start = start;
}

void Sa11xxUartRxLine::Append(Line& l, uint16_t entry, uint64_t due) {
    if (l.entries.empty()) l.next_due = due;
    l.entries.push_back(entry);
    l.due.push_back(due);
}

void Sa11xxUartRxLine::ClearEntries(Line& l) {
    l.entries.clear();
    l.due.clear();
    l.next_due = kNever;
}

void Sa11xxUartRxLine::LogNoClock(const Line& l, size_t n) {
    LOG(Caution, "[UART] %s RX: no sample clock, %zu received bytes not latched\n", l.name, n);
}

/* §11.11.5.6 (printed 11-117): "When LBM=1, ... control of the TXD3 and RXD3 pins is given to the
   peripheral pin control (PPC) unit". */
void Sa11xxUartRxLine::LogLoopback(const Line& l, size_t n) {
    LOG(Caution, "[UART] %s RX: loopback mode, %zu bytes on the receive pin not latched\n", l.name,
        n);
}

/* §11.11.5.6 (printed 11-117): "When LBM=1, the output of the transmit serial shifter is directly
   connected to the input of the receive serial shifter internally". */
void Sa11xxUartRxLine::Loop(uint32_t port, uint8_t byte, const Control& ctl, uint64_t now) {
    if ((ctl.utcr3 & kUtcr3Rxe) == 0u || !Runs(port, ctl)) return;
    Line& l = *line_[port];
    StartClock(l, ctl, now);
    Append(l, byte, 0u);
}

bool Sa11xxUartRxLine::Break(uint32_t port, bool low, const Control& ctl, uint64_t now) {
    if ((ctl.utcr3 & kUtcr3Rxe) == 0u || !Runs(port, ctl)) return false;
    Line& l = *line_[port];
    StartClock(l, ctl, now);
    Append(l, low ? kBreakBegin : kBreakEnd, 0u);
    return true;
}

void Sa11xxUartRxLine::PopFront(uint32_t port, uint64_t now) {
    Line& l = *line_[port];
    l.last_land = l.next_due != 0u ? l.next_due : l.half.TicksAt(now);
    l.quiet     = false;
    l.entries.pop_front();
    l.due.pop_front();
    l.next_due = l.due.empty() ? kNever : l.due.front();
}

void Sa11xxUartRxLine::Rescale(uint32_t port, uint64_t now) {
    Line& l = *line_[port];
    if (!l.timed) return;
    if (!l.half.Rescale(now, clock_->ClockRate(), l.half_rate)) {
        emu_.Get<Fatal>().Die("%s: baud rate overflows the cycle clock ratio", l.name);
    }
}

bool Sa11xxUartRxLine::Reframe(Line& l, const Control& old_ctl, const Control& ctl) {
    const uint64_t f0   = FrameBits(old_ctl.utcr0);
    const uint64_t f1   = FrameBits(ctl.utcr0);
    const uint64_t one0 = f0 - ((old_ctl.utcr0 & kUtcr0Sbs) != 0u ? 1u : 0u);
    const uint64_t one1 = f1 - ((ctl.utcr0 & kUtcr0Sbs) != 0u ? 1u : 0u);
    if (f0 == f1 && one0 == one1) return false;
    bool     any      = false;
    uint64_t prev_old = 0, prev_new = 0;
    for (size_t i = 0; i < l.due.size(); ++i) {
        const uint64_t start_old = l.due[i] - (2u * one0 - 1u);
        uint64_t start_new = start_old;
        if (any) {
            start_new = start_old == prev_old + 2u * f0 ? prev_new + 2u * f1
                                                       : std::max(start_old, prev_new + 2u * f1);
        }
        l.due[i] = start_new + 2u * one1 - 1u;
        prev_old = start_old;
        prev_new = start_new;
        any      = true;
    }
    if (!any) return false;
    l.next_start = prev_new + 2u * f1;
    l.next_due   = l.due.front();
    return true;
}

bool Sa11xxUartRxLine::Retime(uint32_t port, const Control& old_ctl, const Control& ctl,
                              uint64_t now) {
    Line& l = *line_[port];
    if (!l.timed) return false;
    bool dropped = false;
    if ((ctl.utcr3 & ~old_ctl.utcr3 & kUtcr3Lbm) != 0u && !l.entries.empty()) {
        LogLoopback(l, l.entries.size());
        ClearEntries(l);
        dropped = true;
    }
    GuestCycleClock::Rate bit{};
    if (!l.owner->BitClock(ctl, bit)) {
        if (l.entries.empty()) return dropped;
        LogNoClock(l, l.entries.size());
        ClearEntries(l);
        return true;
    }
    const bool reframed = Reframe(l, old_ctl, ctl);
    const GuestCycleClock::Rate half = bit.Scaled(2u, 1u);
    if (half == l.half_rate) return dropped || reframed;
    l.half_rate = half;
    Rescale(port, now);
    return true;
}

void Sa11xxUartRxLine::Stop(uint32_t port) {
    Line& l = *line_[port];
    ClearEntries(l);
    l.timed      = false;
    l.next_start = 0u;
    l.last_land  = 0u;
    l.quiet      = true;
}

void Sa11xxUartRxLine::Reset(uint32_t port) {
    {
        std::lock_guard<std::mutex> lk(line_[port]->wire_mtx);
        line_[port]->wire.clear();
    }
    Stop(port);
}

void Sa11xxUartRxLine::Save(uint32_t port, StateWriter& w) {
    const Line& l = *line_[port];
    const TickPosition pos = l.timed ? l.half.PositionAt(clock_->Cycles()) : TickPosition{};
    w.Write<uint8_t>("rx_line_timed", l.timed ? 1u : 0u);
    w.Write<uint64_t>("rx_line_half_ticks", pos.ticks);
    w.Write<uint64_t>("rx_line_half_phase", pos.phase);
    w.Write<uint64_t>("rx_line_half_phase_den", pos.phase_den);
    w.Write<uint64_t>("rx_line_half_rate_num", l.half_rate.num);
    w.Write<uint64_t>("rx_line_half_rate_den", l.half_rate.den);
    w.Write<uint64_t>("rx_line_next_start", l.next_start);
    w.Write<uint64_t>("rx_line_last_land", l.last_land);
    w.Write<uint8_t>("rx_line_quiet", l.quiet ? 1u : 0u);
    w.Write<uint32_t>("rx_line_count", static_cast<uint32_t>(l.entries.size()));
    for (size_t i = 0; i < l.entries.size(); ++i) {
        w.Write("rx_line_entry", l.entries[i]);
        w.Write("rx_line_due", l.due[i]);
    }
}

void Sa11xxUartRxLine::Restore(uint32_t port, StateReader& r) {
    Line& l = *line_[port];
    uint8_t  timed = 0, quiet = 0;
    uint32_t n = 0;
    TickPosition pos;
    r.Read("rx_line_timed", timed);
    r.Read("rx_line_half_ticks", pos.ticks);
    r.Read("rx_line_half_phase", pos.phase);
    r.Read("rx_line_half_phase_den", pos.phase_den);
    r.Read("rx_line_half_rate_num", l.half_rate.num);
    r.Read("rx_line_half_rate_den", l.half_rate.den);
    r.Read("rx_line_next_start", l.next_start);
    r.Read("rx_line_last_land", l.last_land);
    r.Read("rx_line_quiet", quiet);
    ClearEntries(l);
    r.Read("rx_line_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t e = 0;
        uint64_t d = 0;
        r.Read("rx_line_entry", e);
        r.Read("rx_line_due", d);
        Append(l, e, d);
    }
    {
        std::lock_guard<std::mutex> lk(l.wire_mtx);
        l.wire.clear();
    }
    l.timed = timed != 0u;
    l.quiet = quiet != 0u;
    if (!l.timed) return;
    l.half.SetRate(clock_->ClockRate(), l.half_rate);
    l.half.PlaceAt(clock_->Cycles(), pos);
}

REGISTER_SERVICE(Sa11xxUartRxLine);
