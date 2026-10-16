#pragma once

#include <cerfcore/core/service.h>
#include "../dma_burst_fifo.h"
#include "pxa2xx_ac97_link.h"
#include "pxa2xx_dma_port.h"
#include "pxa2xx_frame_slot_law.h"

#include <cstdint>

class Ac97Codec;
class EmulatedMemory;
class GuestCycleClock;
class Pxa2xxAc97Interrupt;
class Pxa2xxDma;
class StateReader;
class StateWriter;

class Pxa2xxAc97OutFifo : public Service, public Pxa2xxDmaPort, public Pxa2xxAc97LinkListener {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static constexpr uint32_t kFifoe = 1u << 4;
    static constexpr uint32_t kFsr   = 1u << 2;

    uint32_t Status(uint64_t now);
    void     ClearStatus(uint64_t now, uint32_t mask);
    void     WriteData(uint64_t now, uint32_t value, bool overrun_is_error);
    uint64_t NextStatusCycle(uint64_t now, uint32_t watched);

    bool     Receive() const override { return false; }
    uint32_t FifoAddress() const override { return FifoPa(); }
    bool     BurstFifo() const override { return true; }
    void     Settle(uint64_t now) override;
    bool     RequestAsserted() const override;
    uint64_t NextRequestCycle(uint64_t now) override;
    void     SetService(uint64_t now, uint32_t burst_words, uint64_t supply_words) override;
    uint64_t Moved() const override { return tx_.Moved(); }
    bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override;
    void     TransmitBlock(uint32_t pa, uint32_t bytes, bool resumed) override;
    void     ReceiveBlock(uint32_t pa, uint32_t bytes) override;
    void     Stopped(uint64_t now, bool end_of_chain) override;

    void OnLinkRun(uint64_t cycle) override;
    void OnLinkStop(uint64_t cycle) override;
    void OnFifoReset(uint64_t cycle, bool cold) override;
    void OnCodecWrite(uint64_t cycle) override;
    void OnLinkEvent() override;

    void Save(StateWriter& w);
    void Restore(StateReader& r);

protected:
    virtual uint32_t    FifoPa() const = 0;
    virtual uint32_t    Request() const = 0;
    virtual const char*    KeyPrefix() const = 0;
    virtual Pxa2xxSlotPace SlotPace() = 0;
    virtual void        OnWord(uint32_t value) = 0;
    virtual void        OnBlock(uint32_t pa, uint32_t bytes) = 0;
    virtual void        OnDmaStopped() = 0;
    virtual void        OnRestored() = 0;

    Ac97Codec*      codec_ = nullptr;
    EmulatedMemory* mem_   = nullptr;

private:
    void SettleTo(uint64_t now);
    bool ServiceRequest() const;
    void CheckPrimed(uint64_t now);
    bool TakeCycle(uint64_t n, uint64_t& cycle) const;

    Pxa2xxAc97Link*      link_  = nullptr;
    GuestCycleClock*     clock_ = nullptr;
    Pxa2xxDma*           dma_   = nullptr;
    Pxa2xxAc97Interrupt* irq_   = nullptr;

    DmaBurstFifo       tx_;
    Pxa2xxFrameSlotLaw law_{Ac97Codec::kFrameRateHz};
    bool     primed_      = false;
    uint64_t prime_frame_ = 0;
    uint64_t out_frames_  = 0;
    bool     out_error_   = false;
};
