#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../dma_burst_fifo.h"
#include "../pxa2xx/pxa2xx_dma_port.h"
#include "../pxa2xx/pxa2xx_frame_slot_law.h"

#include <cstdint>
#include <vector>

class EmulatedMemory;
class Pxa255I2sBitClock;
class Pxa255I2sFifoResetSync;
class Pxa255I2sFifoValues;
class Pxa255I2sHostOutput;
class Pxa255I2sInterrupt;
class Pxa2xxDma;
class StateReader;
class StateWriter;

class Pxa255I2sStream : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Sacr0() const { return sacr0_; }
    uint32_t Sacr1() const { return sacr1_; }
    bool     Enabled() const { return enabled_; }
    bool     InReset() const;
    uint32_t Sasr0(uint64_t now);

    void     Enable(uint64_t now, uint32_t sacr0);
    void     ChangeSacr0(uint64_t now, uint32_t sacr0);
    void     Disable(uint64_t now, uint32_t sacr0);
    void     ResetHeld(uint64_t now);
    void     WriteSacr1(uint64_t now, uint32_t value);
    void     WriteData(uint64_t now, uint32_t value);
    uint32_t ReadData(uint64_t now);
    void     ResetLine();

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    class Port : public Pxa2xxDmaPort {
    public:
        Port(Pxa255I2sStream& i2s, bool receive) : i2s_(i2s), receive_(receive) {}

        bool     Receive() const override { return receive_; }
        uint32_t FifoAddress() const override;
        bool     BurstFifo() const override;
        void     Settle(uint64_t now) override { i2s_.Settle(now); }
        bool     RequestAsserted() const override;
        uint64_t NextRequestCycle(uint64_t now) override;
        void     SetService(uint64_t now, uint32_t burst_words, uint64_t supply_words) override;
        uint64_t Moved() const override { return receive_ ? i2s_.rx_.Moved() : i2s_.tx_.Moved(); }
        bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override {
            return receive_ ? i2s_.RxCycleOfMoved(words, cycle) : i2s_.TxCycleOfMoved(words, cycle);
        }
        void TransmitBlock(uint32_t pa, uint32_t bytes, bool resumed) override;
        void ReceiveBlock(uint32_t pa, uint32_t bytes) override;
        void Stopped(uint64_t now, bool end_of_chain) override;

    private:
        bool DmaOff() const;

        Pxa255I2sStream& i2s_;
        bool             receive_;
    };

    uint32_t TxThreshold() const { return (sacr0_ >> 8) & 0xFu; }
    uint32_t Rfth() const { return (sacr0_ >> 12) & 0xFu; }
    uint32_t RxFreeThreshold() const { return 15u - Rfth(); }
    uint32_t Replay() const;
    uint32_t Record() const;
    bool     Running() const;
    bool     LinkReadsTx() const;
    bool     LinkWritesRx() const;
    bool     PioOnTx(bool write) const;

    void ConfigureThresholds();
    void OnBitClock(uint64_t now, bool was_running);
    void ApplySacr1(uint64_t now, uint32_t old);
    void Settle(uint64_t now);
    void TakeTo(uint64_t frames);
    void PushTo(uint64_t frames);
    uint64_t PushWords(uint64_t m, const std::vector<uint32_t>* values);
    bool TakeCycle(uint64_t n, uint64_t& cycle) const;
    bool PushCycle(uint64_t m, uint64_t& cycle) const;
    bool TxCycleOfMoved(uint64_t words, uint64_t& cycle);
    bool RxCycleOfMoved(uint64_t words, uint64_t& cycle);
    bool TxRise(uint64_t& cycle) const;
    bool RxRise(uint64_t& cycle) const;
    uint64_t NextStatusChange(uint64_t now, uint32_t rising, uint32_t falling);
    void     ClearRx();

    GuestCycleClock*     clock_     = nullptr;
    EmulatedMemory*      mem_       = nullptr;
    Pxa2xxDma*           dma_       = nullptr;
    Pxa255I2sInterrupt*  irq_       = nullptr;
    Pxa255I2sBitClock*   bit_clock_ = nullptr;
    Pxa255I2sFifoValues* values_    = nullptr;
    Pxa255I2sFifoResetSync* resets_ = nullptr;
    Pxa255I2sHostOutput* host_      = nullptr;

    Port tx_port_{*this, false};
    Port rx_port_{*this, true};

    DmaBurstFifo       tx_;
    DmaBurstFifo       rx_;
    Pxa2xxFrameSlotLaw out_law_{1u};
    Pxa2xxFrameSlotLaw in_law_{1u};
    bool     enabled_          = false;
    uint32_t gated_sacr1_      = 0;
    uint64_t out_frames_       = 0;
    uint64_t in_frames_        = 0;

    uint32_t sacr0_ = 0, sacr1_ = 0;

    std::vector<uint8_t> block_;
};
