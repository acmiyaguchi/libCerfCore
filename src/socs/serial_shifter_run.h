#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class SerialShifterRun {
public:
    void     Reset();
    void     Start(uint64_t first);
    void     End();
    uint64_t DueLoads(uint64_t tick, uint64_t frame) const;
    void     Loaded(uint64_t loaded, bool emptied);

    bool     Running() const { return running_; }
    uint64_t RunStart() const { return run_start_; }
    uint64_t RunLoads() const { return run_loads_; }
    bool     NextLoadTick(uint64_t ahead, uint64_t frame, uint64_t& tick) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    bool     running_   = false;
    uint64_t run_start_ = 0;
    uint64_t run_loads_ = 0;
};

}
