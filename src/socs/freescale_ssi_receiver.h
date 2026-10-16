#pragma once

#include "../execution/guest_cycle_clock.h"
#include "freescale_ssi_section.h"
#include "freescale_ssi_timeline.h"

#include <cstdint>

class CerfEmulator;
class StateReader;
class StateWriter;

class FreescaleSsiReceiver {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    FreescaleSsiReceiver(CerfEmulator& emu, uint32_t depth, uint32_t setup_clocks,
                         uint32_t flag_delay_bits)
        : emu_(emu), depth_(depth), line_(emu, setup_clocks, flag_delay_bits, 1u),
          port_{Port(*this, 0u), Port(*this, 1u)} {}

    void Attach();
    void Reset();
    void Configure(const FreescaleSsiSectionControl& c, const FreescaleSsiFrameShape& shape);
    void OnCpuRate();

    void     ReadSrx(uint32_t k);
    uint32_t Level(uint32_t k);
    void     ClearOverrun(uint32_t k);
    uint32_t RxStatus();
    void     ClearLastSlot();
    uint64_t NextStatusCycle(uint32_t watch);

    FreescaleSsiDmaPort& DmaPort(uint32_t k) { return port_[k]; }

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    class Port : public FreescaleSsiDmaPort {
    public:
        Port(FreescaleSsiReceiver& rx, uint32_t k) : rx_(rx), k_(k) {}
        void     SetDmaBurst(uint32_t words) override { rx_.SetDmaBurst(k_, words); }
        void     SetDmaClock(bool running) override { rx_.SetDmaClock(k_, running); }
        uint64_t DmaWords() override { return rx_.DmaWords(k_); }
        bool     CycleOfDmaWords(uint64_t words, uint64_t& cycle) override {
            return rx_.CycleOfDmaWords(k_, words, cycle);
        }

    private:
        FreescaleSsiReceiver& rx_;
        const uint32_t        k_;
    };

    struct Channel {
        bool     fen     = false;
        uint32_t wm      = 0;
        uint32_t level   = 0;
        uint32_t burst   = 0;
        uint64_t moved     = 0;
        bool     overrun   = false;
        bool     dma_clock = true;
    };

    void     SetDmaBurst(uint32_t k, uint32_t words);
    void     SetDmaClock(uint32_t k, bool running);
    uint64_t DmaWords(uint32_t k);
    bool     CycleOfDmaWords(uint32_t k, uint64_t words, uint64_t& cycle);

    uint32_t Depth(uint32_t k) const { return ch_[k].fen ? depth_ : 1u; }
    uint32_t FullLevel(uint32_t k) const;
    uint32_t Threshold(uint32_t k) const { return ch_[k].fen ? FullLevel(k) : 1u; }
    uint32_t Period(uint32_t k) const;
    bool     DmaOn(uint32_t k) const;
    void     ClearFifos();
    void     Drain(uint32_t k);
    void     Land(uint32_t k, uint64_t n);
    uint64_t CycleOfLanding(uint32_t k, uint64_t m);
    void     Settle();

    CerfEmulator&        emu_;
    const uint32_t       depth_;
    FreescaleSsiTimeline line_;
    GuestCycleClock*     clock_ = nullptr;
    Port                 port_[2];
    Channel              ch_[2];
    bool                 dma_ = false;
};
