#pragma once

#include "sa1111_sac_dma_channel.h"

#include <cstdint>

class Sa1111SacFrameClock;
class Sa1111SacHostInput;
class Sa1111SacRxFifo;

class Sa1111SacRxDma : public Sa1111SacDmaChannel {
public:
    explicit Sa1111SacRxDma(CerfEmulator& emu);

    bool ShouldRegister() override;

    uint64_t Requests() const { return requests_; }
    bool     NextRequestCycle(uint64_t& cycle) const;

protected:
    void OnChannelReady() override;
    void FillTo(uint64_t now) override;
    void SettleTo(uint64_t now) override;
    bool PlannedEnd(uint64_t& cycle) const override;
    void OnBlockStart(uint64_t at, bool buffer_b) override;
    void OnBlockDone() override;
    bool KeepStarted(bool buffer_b) const override;
    void OnStopped() override;
    void OnReset(uint64_t now) override;
    Sa1111Sbi::BusGrant CurrentGrant() const override;
    void ApplyGrant(uint64_t now, Sa1111Sbi::BusGrant grant) override;
    void SaveExtra(StateWriter& w) const override;
    void RestoreExtra(StateReader& r, uint64_t now) override;
    void OnRestored() override;

private:
    bool Draining() const;
    bool NextRequest(uint64_t pos, uint64_t cycle, uint32_t level, uint32_t threshold,
                     uint64_t& at, uint64_t& at_cycle) const;
    void MarkFloor(uint64_t now);

    Sa1111SacFrameClock* frames_      = nullptr;
    Sa1111SacRxFifo*     rx_          = nullptr;
    Sa1111SacHostInput*  host_in_     = nullptr;
    bool                 granted_     = false;
    uint32_t             address_     = 0;
    uint64_t             requests_    = 0;
    uint64_t             floor_pos_   = 0;
    uint64_t             floor_cycle_ = 0;
};
