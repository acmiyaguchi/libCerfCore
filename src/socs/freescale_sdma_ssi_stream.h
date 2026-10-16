#pragma once

#include "../execution/guest_cycle_clock.h"
#include "freescale_sdma_bd_ring.h"
#include "freescale_sdma_bus.h"
#include "freescale_ssi_section.h"

#include <cstdint>
#include <functional>

class CerfEmulator;
class StateReader;
class StateWriter;

class FreescaleSdmaSsiStream {
public:
    struct Hooks {
        std::function<void()>                                start;
        std::function<void(uint32_t buffer, uint32_t bytes)> open;
        std::function<void(uint32_t buffer, uint32_t bytes)> retire;
        std::function<void()>                                halt;
    };

    explicit FreescaleSdmaSsiStream(CerfEmulator& emu) : emu_(emu), ring_(emu) {}

    void Attach(FreescaleSdmaChannelHost& host, const char* tag, Hooks hooks);
    void Reset();
    void Claim(const FreescaleSdmaChannelStart& start, FreescaleSsiDmaPort& port,
               uint32_t sample_bytes);
    bool Release(uint32_t channel);
    void Stop();
    void Rearm();
    void SetSdmaClock(bool running);

    bool     Active() const { return active_; }
    uint32_t BurstWords() const { return burst_words_; }

    void Save(StateWriter& w, const char* prefix);
    void Restore(StateReader& r, const char* prefix, FreescaleSsiDmaPort* port);
    void PostRestore();

private:
    void StartBd();
    void OnBdEnd();
    void Halt();

    CerfEmulator&             emu_;
    const char*               tag_   = "";
    FreescaleSdmaChannelHost* host_  = nullptr;
    FreescaleSsiDmaPort*      port_  = nullptr;
    GuestCycleClock*          clock_ = nullptr;
    GuestCycleClock::Event*   event_ = nullptr;
    Hooks                     hooks_;
    FreescaleSdmaBdRing       ring_;
    bool                      active_       = false;
    bool                      sdma_clocked_ = true;
    uint32_t                  sample_bytes_ = 0;
    uint32_t                  burst_words_  = 0;
    uint32_t                  bd_buffer_    = 0;
    uint32_t                  bd_words_     = 0;
    uint64_t                  bd_end_words_ = 0;
    bool                      bd_cont_      = false;
};
