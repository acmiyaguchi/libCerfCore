#pragma once

#include "../execution/guest_cycle_clock.h"
#include "dma_burst_fifo.h"
#include "freescale_ssi_section.h"
#include "freescale_ssi_timeline.h"

#include <cstdint>

class CerfEmulator;
class StateReader;
class StateWriter;

class FreescaleSsiTransmitter {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    FreescaleSsiTransmitter(CerfEmulator& emu, uint32_t depth, uint32_t setup_clocks,
                            uint32_t flag_delay_bits)
        : emu_(emu), depth_(depth), line_(emu, setup_clocks, flag_delay_bits, 0u),
          port_{Port(*this, 0u), Port(*this, 1u)} {}

    void Attach();
    void Reset();
    void Configure(const FreescaleSsiSectionControl& c, const FreescaleSsiFrameShape& shape,
                   bool command_frames);
    void OnCpuRate();

    void WriteStx(uint32_t k);
    void SetCommand(bool pending);
    bool CommandPending();

    uint32_t Level(uint32_t k);
    void     ClearUnderrun(uint32_t k);
    uint32_t TxStatus();
    void     ClearLastSlot();
    uint64_t NextStatusCycle(uint32_t watch);

    FreescaleSsiDmaPort& DmaPort(uint32_t k) { return port_[k]; }

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    class Port : public FreescaleSsiDmaPort {
    public:
        Port(FreescaleSsiTransmitter& tx, uint32_t k) : tx_(tx), k_(k) {}
        void     SetDmaBurst(uint32_t words) override { tx_.SetDmaBurst(k_, words); }
        void     SetDmaClock(bool running) override { tx_.SetDmaClock(k_, running); }
        uint64_t DmaWords() override { return tx_.DmaWords(k_); }
        bool     CycleOfDmaWords(uint64_t words, uint64_t& cycle) override {
            return tx_.CycleOfDmaWords(k_, words, cycle);
        }

    private:
        FreescaleSsiTransmitter& tx_;
        const uint32_t           k_;
    };

    struct Channel {
        DmaBurstFifo fifo;
        bool         fen      = false;
        uint32_t     wm        = 0;
        bool         underrun  = false;
        bool         dma_clock = true;
    };

    void     SetDmaBurst(uint32_t k, uint32_t words);
    void     SetDmaClock(uint32_t k, bool running);
    uint64_t DmaWords(uint32_t k);
    bool     CycleOfDmaWords(uint32_t k, uint64_t words, uint64_t& cycle);

    uint32_t Depth(uint32_t k) const { return ch_[k].fen ? depth_ : 1u; }
    uint32_t EmptyLevel(uint32_t k) const;
    uint32_t Threshold(uint32_t k) const { return ch_[k].fen ? EmptyLevel(k) : 0u; }
    bool     DmaOn(uint32_t k) const;
    void     ClearFifos();
    void     ConfigureFifos();
    void     UpdateSupply();
    void     Refill();
    uint64_t CycleOfTake(uint32_t k, uint64_t m);
    void     Settle();

    CerfEmulator&        emu_;
    const uint32_t       depth_;
    FreescaleSsiTimeline line_;
    GuestCycleClock*     clock_ = nullptr;
    Port                 port_[2];
    Channel              ch_[2];
    bool     dma_            = false;
    bool     command_frames_ = false;
    bool     cmd_pending_    = false;
    uint64_t cmd_from_       = 0;
};
