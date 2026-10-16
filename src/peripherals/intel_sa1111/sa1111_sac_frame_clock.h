#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/cycle_anchored_counter.h"

#include <cstdint>

class StateReader;
class StateWriter;

class Sa1111SacFrameClock : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool     Rescale(uint64_t now, uint64_t cpu_hz, GuestCycleClock::Rate fs);
    void     Stop(uint64_t now);
    uint64_t Position(uint64_t now) const;
    uint64_t CycleOfPosition(uint64_t pos) const { return frames_.CycleOfTick(pos - base_); }

    void Save(StateWriter& w, uint64_t now) const;
    void Restore(StateReader& r, uint64_t now);

private:
    CycleAnchoredCounter frames_;
    uint64_t cpu_hz_  = 0;
    uint64_t fs_num_  = 0;
    uint64_t fs_den_  = 0;
    bool     clocked_ = false;
    uint64_t frozen_  = 0;
    uint64_t base_    = 0;
};
