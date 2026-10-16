#pragma once

#include "omap3530_mcbsp_direction.h"
#include "omap3530_mcbsp_link.h"
#include "omap3530_mcbsp_regs.h"

#include <cstdint>
#include <deque>

namespace cerfcore {

class StateReader;
class StateWriter;

template <uint32_t kPort>
class Omap3530McBspReceiver : public Omap3530McBspDirection<kPort> {
    using Base = Omap3530McBspDirection<kPort>;

public:
    using Base::Base;
    using typename Base::Clock;
    using Change = typename Clock::Change;

    static constexpr uint32_t kBufferWords = kPort == 2u ? 1280u : 128u;

    void OnReady() override;

    void     Settle(uint64_t now) override;
    void     Apply(const Omap3530McBspRegs& regs, Change change, uint64_t now);
    void     Disable();
    uint32_t ReadData();
    uint32_t Occupied();
    bool     Ready();
    bool     Overrun();

    void OnTimingWillChange(uint64_t now) override;
    void OnTimingRestarted(uint64_t now) override;

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore(const Omap3530McBspRegs& regs);

private:
    using typename Base::Dir;
    using Base::kNever;
    using Base::cycles_;
    using Base::clock_;
    using Base::enabled_;
    using Base::Takes;
    using Base::SlotEnd;
    using Base::InitDirection;
    using Base::Revalidate;
    using Base::RefreshLines;
    using Base::RestoreLines;
    using Base::UpdateIrqLine;
    using Base::SetRequestShape;
    using Base::ClearRequest;
    using Base::DropRequest;
    using Base::EndRequestAtThreshold;
    using Base::CountRequestWord;
    using Base::RequestWords;
    using Base::SaveRequest;
    using Base::RestoreRequest;
    using Base::SaveWords;
    using Base::RestoreWords;

    uint32_t Receive(uint64_t t);
    void     Land(uint32_t value);
    void     UpdateRrdy();
    void     Place(uint64_t after);
    void     TakeRegs(const Omap3530McBspRegs& regs);

    bool     RequestCondition() const override;
    uint64_t OccupiedCycle(uint64_t now, uint32_t n) const;
    uint64_t ConditionCycle(uint64_t now) const override;
    bool     IrqLevel() const override;
    uint64_t IrqCycle(uint64_t now) const override;

    uint32_t rrdy_thr_ = 0u;
    uint32_t rintm_    = 0u;
    uint32_t rjust_    = 0u;

    std::deque<uint32_t> rb_;
    bool     rrdy_     = false;
    bool     placed_   = false;
    uint64_t pos_      = 0u;
    bool     rsr_full_ = false;
    uint32_t rsr_word_ = 0u;
};

using Omap3530McBsp1Receiver = Omap3530McBspReceiver<1>;
using Omap3530McBsp2Receiver = Omap3530McBspReceiver<2>;
using Omap3530McBsp3Receiver = Omap3530McBspReceiver<3>;
using Omap3530McBsp4Receiver = Omap3530McBspReceiver<4>;
using Omap3530McBsp5Receiver = Omap3530McBspReceiver<5>;

}
