#pragma once

#include "freescale_ssi_slot_grid.h"

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

class FreescaleSsiTimeline {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    struct Span {
        uint64_t n   = 0;
        uint64_t ord = 0;
        uint64_t to  = 0;
    };

    FreescaleSsiTimeline(CerfEmulator& emu, uint32_t setup_clocks, uint32_t flag_delay_bits,
                         uint32_t lag)
        : grid_(emu, setup_clocks, flag_delay_bits), lag_(lag) {}

    void Attach() { grid_.Attach(); }
    void Reset();
    void SetRoute(uint32_t data, bool network, bool two_channel);
    void Enable(const FreescaleSsiFrameShape& shape, bool run);
    void Disable(bool run);
    bool Reshape(const FreescaleSsiFrameShape& shape);
    void SetRun(bool on);
    void OnCpuRate() { grid_.OnCpuRate(); }

    bool Enabled() const { return enabled_; }
    bool Run() const { return run_; }
    bool Running() const { return enabled_ && grid_.Running(); }
    bool TwoChannel() const { return two_ch_; }
    bool FirstSlot() const { return first_; }
    bool LastSlot() const { return last_slot_; }
    bool FrameDone() const { return frame_done_; }
    void ClearLastSlot() { last_slot_ = false; }

    uint64_t EventsEnd();
    bool     Next(uint64_t end, Span& span);
    void     Finish();
    uint64_t Uses(uint32_t k, uint64_t ord, uint64_t n) const;
    uint64_t NthUse(uint32_t k, uint64_t m) const;
    uint64_t CycleOfEvent(uint64_t slot);
    uint64_t NextFrameStart() const;
    bool     FrameEnded(uint64_t from, uint64_t to) const;
    uint64_t NextStatusCycle(bool first, bool last, bool done);

    void Save(StateWriter& w, const char* prefix);
    void Restore(StateReader& r, const char* prefix);

private:
    uint64_t DataSlots(uint64_t from, uint64_t to) const;
    uint64_t NthUseIn(uint32_t k, uint64_t m, uint64_t from, uint64_t ord) const;
    uint64_t NextSlot(uint64_t from, uint32_t pos) const;
    uint64_t FirstFall() const;
    bool     LastSlotGenerated() const;
    void     Restart();
    void     FinishStop();

    FreescaleSsiSlotGrid grid_;
    const uint32_t       lag_;
    uint32_t data_       = 0;
    bool     network_    = false;
    bool     two_ch_     = false;
    bool     enabled_    = false;
    bool     run_        = false;
    bool     first_      = false;
    bool     last_slot_  = false;
    bool     frame_done_ = false;
    uint64_t settled_    = 0;
    uint64_t ord_        = 0;
    uint64_t start_      = kNever;
    uint64_t stop_       = kNever;
    uint64_t restart_    = kNever;
    uint64_t done_at_    = kNever;
};

}
