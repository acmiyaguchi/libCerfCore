#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../cycle_anchored_counter.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class Imx28Icoll;
class StateReader;
class StateWriter;

/* MCIMX28RM Rev. 2 sections 23.4.1 ROTCTRL and 10.8.8 XBUS. */
struct Imx28TimrotClocks {
    bool                  operating = false;
    GuestCycleClock::Rate xbus{0u, 1u};
};

/* MCIMX28RM Rev. 2 section 23.2: one of timers 0-3 in fixed count or match count mode. */
class Imx28TimrotTimer {
public:
    Imx28TimrotTimer(CerfEmulator& emu, uint32_t index) : emu_(emu), index_(index) {}

    void Init(Imx28Icoll* icoll);
    void Reset();
    void Retime(const Imx28TimrotClocks& clocks);

    uint32_t Ctrl();
    uint32_t Running();
    uint32_t Fixed() const { return fixed_; }
    uint32_t Match() const { return match_; }
    void     WriteCtrl(Imx28SctOp op, uint32_t value);
    void     WriteFixed(uint32_t value);
    void     WriteMatch(uint32_t value);

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore(const Imx28TimrotClocks& clocks);

private:
    static constexpr uint64_t kNoHit = ~0ull;

    bool     MatchMode() const;
    uint64_t Pos(uint64_t now) const;
    uint32_t ValueAt(uint64_t e) const;
    uint64_t NextHit(uint64_t e0) const;
    GuestCycleClock::Rate TickRate() const;
    void     Settle(uint64_t now);
    void     Rebase(uint64_t now);
    void     Apply();
    void     Arm();
    void     OnEvent();
    void     CheckMatchNow();
    void     UpdateLine(bool force);

    CerfEmulator&           emu_;
    const uint32_t          index_;
    Imx28Icoll*             icoll_ = nullptr;
    GuestCycleClock*        clock_ = nullptr;
    GuestCycleClock::Event* event_ = nullptr;
    CycleAnchoredCounter    counter_;
    CounterPhase            phase_;
    Imx28TimrotClocks       clocks_;
    GuestCycleClock::Rate   ratio_core_{0u, 1u};
    GuestCycleClock::Rate   ratio_tick_{0u, 1u};

    uint32_t ctrl_      = 0u;
    uint32_t fixed_     = 0u;
    uint32_t match_     = 0u;
    uint32_t seg_value_ = 0u;
    uint64_t seg_pos_   = 0u;
    uint64_t eval_pos_  = 0u;
    uint64_t anchor_pos_ = 0u;
    bool     ticking_   = false;
    bool     line_      = false;
};

}
