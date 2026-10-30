#pragma once

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

/* MCIMX28RM Rev. 2 sections 31.3.7-31.3.10: the controller's two general-purpose timers. */
class Imx28UsbGpTimers {
public:
    Imx28UsbGpTimers(CerfEmulator& emu, uint32_t base) : emu_(emu), base_(base) {}

    void Reset();
    bool Read(uint32_t off, uint32_t& value) const;
    bool Write(uint32_t off, uint32_t value);
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    CerfEmulator& emu_;
    uint32_t      base_;
    uint32_t      ld_[2]   = {};
    uint32_t      ctrl_[2] = {};
    uint32_t      cnt_[2]  = {};
};

}
