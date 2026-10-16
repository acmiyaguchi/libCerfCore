#pragma once

#include <cstdint>

namespace cerfcore {

class Pxa2xxDmaPort {
public:
    virtual ~Pxa2xxDmaPort() = default;

    virtual bool     Receive() const     = 0;
    virtual uint32_t FifoAddress() const = 0;
    virtual bool     BurstFifo() const   = 0;

    virtual void     Settle(uint64_t now) = 0;
    static constexpr uint64_t kNoRise = ~uint64_t{0};

    virtual bool     RequestAsserted() const = 0;
    virtual uint64_t NextRequestCycle(uint64_t now) = 0;
    virtual void     SetService(uint64_t now, uint32_t burst_words, uint64_t supply_words) = 0;
    virtual uint64_t Moved() const = 0;
    virtual bool     CycleOfMoved(uint64_t words, uint64_t& cycle) = 0;

    virtual void TransmitBlock(uint32_t pa, uint32_t bytes, bool resumed) = 0;
    virtual void ReceiveBlock(uint32_t pa, uint32_t bytes)  = 0;
    virtual void Stopped(uint64_t now, bool end_of_chain)   = 0;
};

}
