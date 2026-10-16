#pragma once

#include "freescale_sdma_bus.h"
#include "freescale_sdma_regs.h"

#include <cstdint>

class CerfEmulator;
class StateReader;
class StateWriter;

struct FreescaleSdmaBd {
    uint32_t pa     = 0;
    uint32_t buffer = 0;
    uint32_t count  = 0;
    bool     cont   = false;
};

class FreescaleSdmaBdRing {
public:
    explicit FreescaleSdmaBdRing(CerfEmulator& emu) : emu_(emu) {}

    void Begin(const FreescaleSdmaChannelStart& start);
    bool Open(FreescaleSdmaBd& bd) const;
    bool Retire(FreescaleSdmaChannelHost& host, bool cont);

    uint32_t Channel() const { return channel_; }
    uint32_t Current() const { return cur_pa_; }

    void Save(StateWriter& w, const char* prefix) const;
    void Restore(StateReader& r, const char* prefix);

private:
    CerfEmulator& emu_;
    uint32_t      channel_ = 0;
    uint32_t      ccb_pa_  = 0;
    uint32_t      base_    = 0;
    uint32_t      stride_  = 0;
    uint32_t      cur_pa_  = 0;
};
