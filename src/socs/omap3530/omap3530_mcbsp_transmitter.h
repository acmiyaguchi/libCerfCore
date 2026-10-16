#pragma once

#include "omap3530_mcbsp_direction.h"
#include "omap3530_mcbsp_link.h"
#include "omap3530_mcbsp_regs.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

template <uint32_t kPort>
class Omap3530McBspTransmitter : public Omap3530McBspDirection<kPort> {
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
    void     WriteData(uint32_t value);
    uint32_t FreeLocations();
    bool     ShiftRegisterLoaded();
    bool     Ready();

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

    uint32_t XbCount() const;
    void     PlaceFresh(uint64_t now);
    bool     Reporting() const;
    void     FillUntil(uint64_t frame);
    void     FillComplete(uint64_t now);
    void     ReportNew();
    void     Report(uint64_t t, uint32_t value);
    void     FlushPending();
    void     Enable(uint64_t now);
    void     Replace(uint64_t now);
    void     TakeRegs(const Omap3530McBspRegs& regs);

    bool     FreeAtLeast(uint32_t n) const;
    uint64_t FreeCycle(uint64_t now, uint32_t n) const;
    bool     RequestCondition() const override;
    uint64_t ConditionCycle(uint64_t now) const override;
    bool     IrqLevel() const override;
    uint64_t IrqCycle(uint64_t now) const override;

    uint32_t xrdy_thr_ = 0u;
    uint32_t xintm_    = 0u;

    std::deque<uint32_t> words_;
    bool     fresh_    = false;
    bool     placed_   = false;
    uint64_t pos_x_       = 0u;
    size_t   reported_    = 0u;
    uint32_t dx_value_    = 0u;
    uint32_t shifted_     = 0u;
    uint64_t next_report_ = 0u;

    bool                                   pend_valid_   = false;
    bool                                   pend_reverse_ = false;
    uint32_t                               pend_before_  = 0u;
    Omap3530McBspDxFrame                   pend_frame_;
    std::vector<Omap3530McBspFrame::Slot>  pend_slots_;
    std::vector<uint32_t> pend_vals_;
    std::vector<uint8_t>  pend_set_;
};

using Omap3530McBsp1Transmitter = Omap3530McBspTransmitter<1>;
using Omap3530McBsp2Transmitter = Omap3530McBspTransmitter<2>;
using Omap3530McBsp3Transmitter = Omap3530McBspTransmitter<3>;
using Omap3530McBsp4Transmitter = Omap3530McBspTransmitter<4>;
using Omap3530McBsp5Transmitter = Omap3530McBspTransmitter<5>;

}
