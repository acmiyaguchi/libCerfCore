#pragma once

#include "../execution/guest_cycle_clock.h"
#include "freescale_ssi_receiver.h"
#include "freescale_ssi_section.h"
#include "freescale_ssi_slot_grid.h"
#include "freescale_ssi_status.h"
#include "freescale_ssi_transmitter.h"

#include <cstdint>
#include <functional>
#include <vector>

class CerfEmulator;
class StateReader;
class StateWriter;

struct FreescaleSsiCoreConfig {
    uint32_t                depth;
    uint32_t                setup_clocks;
    FreescaleSsiStatusRules status;
    bool                    channel_requests;
    bool                    tag_slots;
    bool                    variable_frames;
    bool                    test_register;
};

class FreescaleSsiCore {
public:
    FreescaleSsiCore(CerfEmulator& emu, uint32_t ssi, const FreescaleSsiCoreConfig& config);

    void Attach(std::function<GuestCycleClock::Rate()> ssi_clock, std::function<bool()> clocked,
                std::function<void(bool)> line);
    void Reset();
    bool Read(uint32_t off, uint32_t& value);
    bool Write(uint32_t off, uint32_t value);
    void OnCpuRate();
    void OnClockChange();
    void RegisterControlListener(std::function<void()> fn);

    FreescaleSsiDmaPort&   TxPort(uint32_t k) { return tx_port_[k]; }
    FreescaleSsiDmaPort&   RxPort(uint32_t k) { return rx_port_[k]; }
    GuestCycleClock::Rate  TxFifo0WordRate();

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();

private:
    class StatusPort : public FreescaleSsiDmaPort {
    public:
        StatusPort(FreescaleSsiDmaPort& port, FreescaleSsiStatus& status)
            : port_(port), status_(status) {}
        void SetDmaBurst(uint32_t words) override {
            port_.SetDmaBurst(words);
            status_.Update();
        }
        void SetDmaClock(bool running) override {
            port_.SetDmaClock(running);
            status_.Update();
        }
        uint64_t DmaWords() override { return port_.DmaWords(); }
        bool     CycleOfDmaWords(uint64_t words, uint64_t& cycle) override {
            return port_.CycleOfDmaWords(words, cycle);
        }

    private:
        FreescaleSsiDmaPort& port_;
        FreescaleSsiStatus&  status_;
    };

    bool     Enabled() const;
    bool     Ac97() const;
    bool     Network() const;
    bool     Sync() const;
    bool     TxRun() const;
    uint32_t Stcr() const;
    uint32_t Srcr() const;
    uint32_t Sfcsr();

    FreescaleSsiFrameShape     ClockShape(uint32_t cr, uint32_t ccr);
    FreescaleSsiFrameShape     SectionShape(uint32_t cr, uint32_t ccr);
    FreescaleSsiFrameShape     TxShape();
    FreescaleSsiFrameShape     RxShape();
    uint32_t                   TxData(const FreescaleSsiFrameShape& shape) const;
    uint32_t                   RxData(const FreescaleSsiFrameShape& shape) const;
    FreescaleSsiSectionControl Section(bool tx, const FreescaleSsiFrameShape& shape) const;
    FreescaleSsiStatusControl  StatusControl() const;

    void Apply(bool notify);
    void Notify();
    void WriteSacnt(uint32_t value);

    CerfEmulator&                          emu_;
    const uint32_t                         ssi_;
    const FreescaleSsiCoreConfig           cfg_;
    FreescaleSsiTransmitter                tx_;
    FreescaleSsiReceiver                   rx_;
    FreescaleSsiStatus                     status_;
    StatusPort                             tx_port_[2];
    StatusPort                             rx_port_[2];
    std::function<GuestCycleClock::Rate()> ssi_clock_;
    std::function<bool()>                  clocked_;
    std::vector<std::function<void()>>     listeners_;
    uint32_t scr_ = 0, sier_ = 0, stcr_ = 0, srcr_ = 0, stccr_ = 0, srccr_ = 0, sfcsr_ = 0;
    uint32_t str_ = 0, sacnt_ = 0, sacadd_ = 0, sacdat_ = 0, tx_tag_ = 0, stmsk_ = 0, srmsk_ = 0;
    uint32_t saccst_ = 0, cmd_bits_ = 0;
};
