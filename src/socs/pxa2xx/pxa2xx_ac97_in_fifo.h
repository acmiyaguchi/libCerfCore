#pragma once

#include <cerfcore/core/service.h>
#include "../dma_burst_fifo.h"
#include "pxa2xx_ac97_link.h"
#include "pxa2xx_dma_port.h"

#include <cstdint>
#include <deque>

namespace cerfcore {

class Ac97Codec;
class EmulatedMemory;
class GuestCycleClock;
class Pxa2xxAc97Interrupt;
class Pxa2xxDma;
class Pxa2xxSlotSource;
class StateReader;
class StateWriter;

class Pxa2xxAc97InFifo : public Service, public Pxa2xxDmaPort, public Pxa2xxAc97LinkListener {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static constexpr uint32_t kFifoe = 1u << 4;
    static constexpr uint32_t kEoc   = 1u << 3;
    static constexpr uint32_t kFsr   = 1u << 2;

    uint32_t Status(uint64_t now);
    void     ClearStatus(uint64_t now, uint32_t mask);
    uint16_t ReadData(uint64_t now, bool underrun_is_error);
    uint64_t NextStatusCycle(uint64_t now, uint32_t watched);

    bool     Receive() const override { return true; }
    uint32_t FifoAddress() const override { return FifoPa(); }
    bool     BurstFifo() const override { return true; }
    void     Settle(uint64_t now) override;
    bool     RequestAsserted() const override;
    uint64_t NextRequestCycle(uint64_t now) override;
    void     SetService(uint64_t now, uint32_t burst_words, uint64_t supply_words) override;
    uint64_t Moved() const override { return fifo_.Moved(); }
    bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override;
    void     TransmitBlock(uint32_t pa, uint32_t bytes, bool resumed) override;
    void     ReceiveBlock(uint32_t pa, uint32_t bytes) override;
    void     Stopped(uint64_t now, bool end_of_chain) override;

    void OnLinkRun(uint64_t cycle) override;
    void OnLinkStop(uint64_t cycle) override;
    void OnFifoReset(uint64_t cycle, bool cold) override;
    void OnCodecWrite(uint64_t cycle) override;
    void OnLinkEvent() override;

    virtual void Save(StateWriter& w);
    virtual void Restore(StateReader& r);

protected:
    virtual uint32_t          FifoPa() const = 0;
    virtual uint32_t          Request() const = 0;
    virtual const char*       KeyPrefix() const = 0;
    virtual Pxa2xxSlotSource& Source() = 0;
    virtual bool              KeepsValues() const = 0;
    virtual uint16_t          SlotValue(uint64_t n) = 0;
    virtual void              OnRun() = 0;
    virtual void              OnWrite(uint64_t next_frame) = 0;

    Pxa2xxAc97Link*  link_  = nullptr;
    Ac97Codec*       codec_ = nullptr;
    GuestCycleClock* clock_ = nullptr;
    Pxa2xxDma*       dma_   = nullptr;

private:
    void     SettleTo(uint64_t now);
    void     Discard(bool cold);
    uint32_t Filled() const;
    bool     ServiceRequest() const;
    bool     PushCycle(uint64_t m, uint64_t& cycle);

    EmulatedMemory*      mem_ = nullptr;
    Pxa2xxAc97Interrupt* irq_ = nullptr;
    DmaBurstFifo         fifo_;
    uint64_t             frames_      = 0;
    uint64_t             written_     = 0;
    bool                 error_       = false;
    bool                 eoc_         = false;
    std::deque<uint16_t> held_;
};

}
