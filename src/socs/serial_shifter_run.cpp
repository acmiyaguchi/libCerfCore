#include "serial_shifter_run.h"

#include <cerfcore/state/state_stream.h>

void SerialShifterRun::Reset() {
    running_   = false;
    run_start_ = 0u;
    run_loads_ = 0u;
}

void SerialShifterRun::Start(uint64_t first) {
    running_   = true;
    run_start_ = first;
    run_loads_ = 0u;
}

void SerialShifterRun::End() {
    running_ = false;
}

uint64_t SerialShifterRun::DueLoads(uint64_t tick, uint64_t frame) const {
    if (!running_ || tick < run_start_) return 0u;
    const uint64_t due = (tick - run_start_) / frame + 1u;
    return due > run_loads_ ? due - run_loads_ : 0u;
}

void SerialShifterRun::Loaded(uint64_t loaded, bool emptied) {
    run_loads_ += loaded;
    if (emptied) running_ = false;
}

bool SerialShifterRun::NextLoadTick(uint64_t ahead, uint64_t frame, uint64_t& tick) const {
    if (!running_ || ahead == 0u) return false;
    tick = run_start_ + (run_loads_ + ahead - 1u) * frame;
    return true;
}

void SerialShifterRun::Save(StateWriter& w) const {
    w.Write<uint8_t>("run_running", running_ ? 1u : 0u);
    w.Write<uint64_t>("run_start_half_tick", run_start_);
    w.Write<uint64_t>("run_loads", run_loads_);
}

void SerialShifterRun::Restore(StateReader& r) {
    uint8_t running = 0;
    r.Read("run_running", running);
    r.Read("run_start_half_tick", run_start_);
    r.Read("run_loads", run_loads_);
    running_ = running != 0u;
}
