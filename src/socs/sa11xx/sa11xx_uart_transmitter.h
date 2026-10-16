#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "sa11xx_dma_port.h"
#include "sa11xx_dma_transmit_fifo.h"
#include "sa11xx_uart_regs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace cerfcore {

class Sa11xxDma;
class Sa11xxUartBase;
class Sa11xxUartReceiver;
class StateReader;
class StateWriter;

class Sa11xxUartTransmitter : public Service {
public:
    using Bytes   = std::vector<uint8_t>;
    using Control = sa11xx_uart::Control;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(Sa11xxUartBase* owner, uint32_t utdr, uint32_t device_select,
                    uint32_t rx_port, Sa11xxDmaTransmitFifo& fifo);
    bool WriteControl(uint32_t port, const Control& c, Bytes& out);
    void ClockChanged(uint32_t port, Bytes& out);
    void Write(uint32_t port, uint8_t value, Bytes& out);
    void Settle(uint32_t port, Bytes& out);
    void Reset(uint32_t port);
    void NotifyDma();

    bool     Tfs(uint32_t port) const { return ch_[port]->tfs.load(std::memory_order_acquire); }
    bool     Tnf(uint32_t port) const;
    uint64_t DmaMoved(uint32_t port) const { return ch_[port]->fifo->Moved(); }

    void Save(uint32_t port, StateWriter& w);
    void Restore(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 3u;

    struct Channel;

    class Port : public Sa11xxDmaPort {
    public:
        Port(Sa11xxUartTransmitter& t, uint32_t index) : t_(t), index_(index) {}
        uint32_t DeviceAddress() const override;
        bool     Receive() const override { return false; }
        uint32_t DatumBytes() const override { return 1u; }
        void     Settle(uint64_t) override { t_.PortSettle(index_); }
        void     SetSupply(uint64_t, uint64_t words) override { t_.SetDmaSupply(index_, words); }
        uint64_t Moved() const override { return t_.DmaMoved(index_); }
        bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override {
            return t_.CycleOfMoved(index_, words, cycle);
        }
        GuestCycleClock::Rate WordRate() const override { return t_.FrameRate(*t_.ch_[index_]); }

    private:
        Sa11xxUartTransmitter& t_;
        const uint32_t         index_;
    };

    struct Channel {
        Channel(Sa11xxUartTransmitter& t, uint32_t index) : port(t, index) {}
        Port                   port;
        Sa11xxUartBase*        owner   = nullptr;
        uint32_t               utdr    = 0;
        uint32_t               rx_port = 0;
        Control                ctl{};
        GuestCycleClock::Rate  rate{};
        Sa11xxDmaTransmitFifo* fifo    = nullptr;
        std::vector<uint16_t>  taken;
        bool                   clocked    = false;
        std::atomic<bool>      tfs{false};
        bool                   low        = false;
        bool                   break_sent = false;
    };

    static GuestCycleClock::Rate FrameRate(const Channel& c);
    static bool LoopLow(const Control& ctl);

    void Drain(Channel& c, Bytes& out);
    void SendLoopBreak(Channel& c);
    void EndLoopLow(Channel& c);
    void Clock(Channel& c);
    void StopTransmit(Channel& c);
    void Publish(Channel& c);
    void PortSettle(uint32_t port);
    void SetDmaSupply(uint32_t port, uint64_t words);
    bool CycleOfMoved(uint32_t port, uint64_t words, uint64_t& cycle);

    GuestCycleClock*    clock_ = nullptr;
    Sa11xxDma*          dma_   = nullptr;
    Sa11xxUartReceiver* rx_    = nullptr;
    std::array<std::unique_ptr<Channel>, kPorts> ch_{};
    uint32_t         count_ = 0;
};

}
