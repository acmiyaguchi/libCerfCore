#pragma once

#include "sa1111_sac_dma_channel.h"

#include <cstdint>

namespace cerfcore {

class Sa1111SacHostOutput;
class Sa1111SacTxStream;

class Sa1111SacDma : public Sa1111SacDmaChannel {
public:
    explicit Sa1111SacDma(CerfEmulator& emu);

    bool ShouldRegister() override;

    uint32_t WordsRequested() const;
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
    uint32_t Threshold() const;

    Sa1111SacTxStream*   stream_ = nullptr;
    Sa1111SacHostOutput* host_   = nullptr;
};

}
