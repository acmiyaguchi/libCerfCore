#pragma once

#include "sa1111_unit.h"
#include "sa1111_sac_request_lines.h"
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

class Sa1111SacDma;
class Sa1111SacFrameClock;
class Sa1111SacHostInput;
class Sa1111SacHostOutput;
class Sa1111SacL3;
class Sa1111SacRxDma;
class Sa1111SacRxFifo;
class Sa1111SacTxStream;

class Sa1111Sac : public Sa1111Unit {
public:
    using Sa1111Unit::Sa1111Unit;

    bool ShouldRegister() override;

    uint32_t MmioBase() const override { return 0x40000600u; }
    uint32_t MmioSize() const override { return 0x00000200u; }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    void     OnUnitReady() override;
    void     OnChipReset(bool held) override;
    uint32_t UnitReadWord (uint32_t addr) override;
    void     UnitWriteWord(uint32_t addr, uint32_t value) override;

private:
    void     OnSystemClockWrite();
    void     WriteRegister(uint32_t addr, uint32_t value);
    void     ResetRegisters(uint64_t now);
    bool     RstActive() const;
    void     WriteSacr0(uint32_t value);
    void     WriteSacr1(uint32_t value);
    void     WriteSadr(uint64_t now, uint32_t value);
    uint32_t ReadSadr(uint64_t now);
    void     UpdateSerializer(uint64_t now);
    bool     SerializerWanted() const;
    bool     RecordingWanted() const;
    uint32_t ThresholdLevel() const;
    uint32_t RxThresholdLevel() const;
    bool     Enabled() const;
    uint32_t FifoStatus(uint64_t now);
    void     EvaluateDma(uint64_t now);
    void     CatchUpDma(uint64_t now);
    Sa1111SacRequestLines::Inputs LineInputs() const;
    void     PublishRequestLines(uint64_t now);
    void     RefreshSourceEdges(uint64_t now);
    void     SyncRequestLines();
    void     RescaleFrames(uint64_t now, GuestCycleClock::Rate fs);
    void     ApplyFrameRate(uint64_t now);

    GuestCycleClock*        clock_   = nullptr;
    Sa1111SacDma*           dma_     = nullptr;
    Sa1111SacRxDma*         rx_dma_  = nullptr;
    Sa1111SacTxStream*      stream_  = nullptr;
    Sa1111SacFrameClock*    frames_  = nullptr;
    Sa1111SacRxFifo*        rx_      = nullptr;
    Sa1111SacRequestLines*  lines_   = nullptr;
    Sa1111SacHostOutput*    host_    = nullptr;
    Sa1111SacHostInput*     host_in_ = nullptr;
    Sa1111SacL3*            l3_      = nullptr;

    /* SA-1111 Developer's Manual Table 7-7 note: "The power-up/reset default value of this
       register is 7700h." */
    static constexpr uint32_t kSacr0Reset = 0x7700u;

    uint32_t sacr0_ = kSacr0Reset, sacr1_ = 0, sacr2_ = 0;
    uint32_t accar_ = 0, accdr_ = 0, saitr_ = 0;
    bool     clock_on_ = false;
};
