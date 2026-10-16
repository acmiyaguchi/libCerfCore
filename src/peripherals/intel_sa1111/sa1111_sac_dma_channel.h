#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "sa1111_sac_dma_control.h"
#include "sa1111_sbi.h"

#include <cstdint>
#include <functional>

class Sa1111SacRequestLines;
class Sa1111SystemController;
class StateReader;
class StateWriter;

class Sa1111SacDmaChannel : public Service {
public:
    struct Names {
        Sa1111SacDmaControl::Keys keys;
        const char* running;
        const char* buffer_b;
        const char* words_left;
        const char* tag;
        const char* control;
        uint8_t     done_a;
        uint8_t     done_b;
    };

    Sa1111SacDmaChannel(CerfEmulator& emu, const Names& names);

    void OnReady() override;

    void SetFifoControlReader(std::function<Sa1111SacDmaControl::Fifo()> read);
    void SetChangeListener(std::function<void(uint64_t now)> on_change);

    uint32_t ReadRegister(uint32_t index);
    void     WriteRegister(uint32_t index, uint32_t value);

    void Evaluate(uint64_t now);
    void CatchUp(uint64_t now);
    void Reset(uint64_t now);

    void Save(StateWriter& w) const;
    void Restore(StateReader& r, uint64_t now);
    void PostRestore();

protected:
    virtual void OnChannelReady() = 0;
    virtual void FillTo(uint64_t now) = 0;
    virtual void SettleTo(uint64_t now) = 0;
    virtual bool PlannedEnd(uint64_t& cycle) const = 0;
    virtual void OnBlockStart(uint64_t at, bool buffer_b) = 0;
    virtual void OnBlockDone() = 0;
    virtual bool KeepStarted(bool buffer_b) const = 0;
    virtual void OnStopped() = 0;
    virtual void OnReset(uint64_t now) = 0;
    virtual Sa1111Sbi::BusGrant CurrentGrant() const = 0;
    virtual void ApplyGrant(uint64_t now, Sa1111Sbi::BusGrant grant) = 0;
    virtual void SaveExtra(StateWriter& w) const = 0;
    virtual void RestoreExtra(StateReader& r, uint64_t now) = 0;
    virtual void OnRestored() = 0;

    Sa1111SacDmaControl::Fifo Fifo() const { return fifo_(); }

    Sa1111SacDmaControl     ctl_;
    GuestCycleClock*        clock_      = nullptr;
    Sa1111SystemController* sysc_       = nullptr;
    bool                    running_    = false;
    bool                    buffer_b_   = false;
    uint32_t                words_left_ = 0;
    uint64_t                end_cycle_  = 0;

private:
    void WriteControl(uint32_t value);
    void TryStartNext(uint64_t at, bool prefer_b);
    void StartBlock(uint64_t at, bool buffer_b);
    void CompleteBlock(uint64_t at);
    void Stop();
    void ArmDone(uint64_t now);
    void OnGrantChange();

    Names                                      names_;
    std::function<Sa1111SacDmaControl::Fifo()> fifo_;
    std::function<void(uint64_t now)>          on_change_ = [](uint64_t) {};
    GuestCycleClock::Event*                    done_ev_   = nullptr;
    Sa1111SacRequestLines*                     lines_     = nullptr;
};
