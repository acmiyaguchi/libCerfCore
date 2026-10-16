#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class EmulatedMemory;
namespace cerfengine { class HostRequestChannel; }
class StateReader;
class StateWriter;

class Pr31x00UartDma : public Service {
public:
    /* TMPR3911 Table 16.2.2 (page 16-3): UARTnRXINT "receiver holding register becomes full",
       UARTnRXOVERRUNINT "receiver shift register becomes full when both holding register and
       PRXHOLD are full", UARTnDMAFULLINT / UARTnDMAHALFINT. */
    struct RxInts {
        bool rx        = false;
        bool overrun   = false;
        bool dma_half  = false;
        bool dma_full  = false;
    };
    using RxIntFn    = std::function<void(const RxInts&)>;
    using LineIdleFn = std::function<void()>;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(const char* source, RxIntFn raise_ints, LineIdleFn on_line_idle);

    void SetBuffer(uint32_t port, uint32_t pa);
    void SetLength(uint32_t port, uint32_t bytes);
    void SetControl(uint32_t port, bool receiver_on, bool armed);
    void ResetLine(uint32_t port);
    void SetClockRunning(uint32_t port, bool running);

    uint32_t Count(uint32_t port) const;
    uint32_t Length(uint32_t port) const;
    bool     LineIdle(uint32_t port) const;
    bool     RxHoldFull(uint32_t port) const;
    bool     PrxHoldFull(uint32_t port) const;

    void Receive(uint32_t port, const uint8_t* data, size_t n);
    void TransmitBlock(uint32_t port, std::vector<uint8_t>& out);

    void SaveState(uint32_t port, StateWriter& w);
    void RestoreState(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 2u;

    struct Channel {
        const char* source = "";
        RxIntFn     raise_ints;
        LineIdleFn  on_line_idle;

        mutable std::mutex mu;

        uint32_t buffer_pa   = 0;
        uint32_t length      = 0;
        uint32_t count       = 0;
        bool     armed       = false;
        bool     receiver_on = false;

        uint8_t rxhold       = 0;
        uint8_t prxhold      = 0;
        bool    rxhold_full  = false;
        bool    prxhold_full = false;
        uint8_t rxshift      = 0;
        bool    rxshift_full = false;

        std::vector<uint8_t> wire;
        size_t               wire_pos = 0;

        bool clock_running = false;
    };

    Channel&       Ch(uint32_t port);
    const Channel& Ch(uint32_t port) const;
    void           LandPending(Channel& c);
    void           ReceiveByteLocked(Channel& c, uint8_t byte, RxInts& ints);
    void           StoreLocked(Channel& c, uint8_t byte, RxInts& ints);
    void           DrainHoldsLocked(Channel& c, RxInts& ints);
    static void    Notify(Channel& c, const RxInts& ints, bool line_idle);

    std::array<std::unique_ptr<Channel>, kPorts> ch_;
    uint32_t                                     count_ = 0;

    EmulatedMemory*     mem_           = nullptr;
    cerfengine::HostRequestChannel* host_requests_ = nullptr;
};
