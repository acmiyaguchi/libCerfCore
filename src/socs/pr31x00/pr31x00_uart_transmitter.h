#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace cerfcore {

class Pr31x00UartDma;
class StateReader;
class StateWriter;

class Pr31x00UartTransmitter : public Service {
public:
    /* TMPR3911 Table 16.2.2 (page 16-3): UARTnTXINT "transmit holding register becomes empty",
       UARTnEMPTYINT, and UARTnDMAFULLINT / UARTnDMAHALFINT. */
    struct TxInts {
        bool sent     = false;
        bool dma_half = false;
        bool dma_full = false;
    };
    using DeliverFn = std::function<void(const uint8_t* data, size_t n)>;
    using TxIntFn   = std::function<void(const TxInts&)>;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(uint32_t dma_port, DeliverFn deliver, TxIntFn raise_ints);

    void WriteHold(uint32_t port, uint8_t byte);
    void StartDma(uint32_t port);
    void CancelDma(uint32_t port);
    void SetClockRunning(uint32_t port, bool running);
    void ResetLine(uint32_t port);
    bool Empty(uint32_t port) const;

    void SaveState(uint32_t port, StateWriter& w);
    void RestoreState(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 2u;

    struct Channel {
        uint32_t  dma_port = 0;
        DeliverFn deliver;
        TxIntFn   raise_ints;

        mutable std::mutex mu;

        uint8_t hold          = 0;
        bool    hold_full     = false;
        bool    dma_pending   = false;
        bool    clock_running = false;
    };

    Channel&       Ch(uint32_t port);
    const Channel& Ch(uint32_t port) const;
    void           RunDma(Channel& c);

    std::array<std::unique_ptr<Channel>, kPorts> ch_;
    uint32_t                                     count_ = 0;

    Pr31x00UartDma* dma_ = nullptr;
};

}
