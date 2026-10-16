#pragma once

#include "omap3530_mcbsp_port_clock.h"
#include "omap3530_mcbsp_request_lines.h"

#include <cstdint>
#include <deque>

class StateReader;
class StateWriter;

template <uint32_t kPort>
class Omap3530McBspDirection : public Omap3530McBspRequestLines, public Omap3530McBspTimingListener {
public:
    using Clock = Omap3530McBspPortClock<kPort>;

    explicit Omap3530McBspDirection(CerfEmulator& emu) : Omap3530McBspRequestLines(emu) {}

    bool ShouldRegister() override;

    bool Enabled() const { return enabled_; }

    void OnTimingRescaled(uint64_t now) override;

protected:
    using Dir = Omap3530McBspDir;

    void     InitDirection(Dir dir, uint32_t source, int irq);
    uint32_t Takes() const { return clock_->Layout(Direction()).TakeCount(); }
    uint64_t SlotEnd(uint64_t t) const { return clock_->SlotEndCycle(Direction(), t); }
    void     Revalidate(uint64_t now);
    void     SaveWords(StateWriter& w, const std::deque<uint32_t>& words) const;
    void     RestoreWords(StateReader& r, std::deque<uint32_t>& words) const;

    Clock* clock_   = nullptr;
    bool   enabled_ = false;
};
