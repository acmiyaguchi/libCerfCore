#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "sa11xx_dma_port.h"
#include "sa11xx_dma_receive_fifo.h"
#include "sa11xx_uart_regs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cerfengine { class HostRequestChannel; }
class Sa11xxDma;
class Sa11xxUartBase;
class Sa11xxUartRxLine;
class StateReader;
class StateWriter;

class Sa11xxUartReceiver : public Service {
public:
    using Control = sa11xx_uart::Control;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(Sa11xxUartBase* owner, uint32_t utdr, uint32_t device_select,
                    const char* name, Sa11xxDmaReceiveFifo& fifo);
    void     Push(uint32_t port, const uint8_t* data, size_t n);
    void     PushNow(uint32_t port, const uint8_t* data, size_t n);
    void     Loop(uint32_t port, uint8_t byte);
    bool     LoopBreak(uint32_t port, bool low);
    void     WriteControl(uint32_t port, uint64_t now, const Control& c);
    void     Settle(uint32_t port, uint64_t now);
    void     ClockChanged(uint32_t port, uint64_t now);
    uint8_t  Pop(uint32_t port, uint64_t now);
    uint32_t Utsr0(uint32_t port) const;
    uint32_t Utsr1(uint32_t port) const;
    void     ClearStatus(uint32_t port, uint64_t now, uint32_t mask);
    bool     InterruptRequest(uint32_t port) const;
    void     Reset(uint32_t port);
    uint64_t DmaMoved(uint32_t port) const { return ch_[port]->fifo->Moved(); }
    bool     TakeReplan(uint32_t port) {
        Channel& c = *ch_[port];
        const bool replan = c.replan;
        c.replan = false;
        return replan;
    }

    void Save(uint32_t port, StateWriter& w);
    void Restore(uint32_t port, StateReader& r);
    void PostRestore(uint32_t port);

private:
    static constexpr uint32_t kPorts = 3u;

    class Port : public Sa11xxDmaPort {
    public:
        Port(Sa11xxUartReceiver& r, uint32_t index) : r_(r), index_(index) {}
        uint32_t DeviceAddress() const override;
        bool     Receive() const override { return true; }
        uint32_t DatumBytes() const override { return 1u; }
        void     Settle(uint64_t now) override { r_.Settle(index_, now); }
        void     SetSupply(uint64_t now, uint64_t words) override {
            r_.SetDmaSupply(index_, now, words);
        }
        uint64_t Moved() const override { return r_.ch_[index_]->fifo->Moved(); }
        bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override;
        GuestCycleClock::Rate WordRate() const override;
        bool     FillReceived(uint32_t pa, uint32_t bytes) override {
            return r_.ch_[index_]->fifo->Fill(pa, bytes);
        }

    private:
        Sa11xxUartReceiver& r_;
        const uint32_t      index_;
    };

    struct Channel {
        Channel(Sa11xxUartReceiver& r, uint32_t index) : port(r, index) {}
        Port                    port;
        GuestCycleClock::Event* ev         = nullptr;
        Sa11xxUartBase*         owner      = nullptr;
        Sa11xxDmaReceiveFifo*   fifo       = nullptr;
        const char*             name       = "";
        uint32_t                utdr       = 0;
        uint32_t                line       = 0;
        Control                 ctl{};
        bool                    rid        = false;
        bool                    rbb        = false;
        bool                    reb        = false;
        bool                    break_lock = false;
        bool                    replan     = false;
    };

    static bool Rfs(uint32_t utcr3, const Sa11xxDmaReceiveFifo& fifo);

    bool CycleOfMoved(const Channel& c, uint64_t words, uint64_t& cycle) const;
    void SettleChannel(Channel& c, uint64_t now);
    void Arm(Channel& c, uint64_t now);
    void OnEvent(uint32_t port);
    void OnCpuRate();
    void Store(Channel& c, uint8_t byte, int32_t tags);
    void Take(Channel& c, uint16_t entry);
    void StopReceive(Channel& c);
    void SetDmaSupply(uint32_t port, uint64_t now, uint64_t words);
    void OnHostRequest();

    GuestCycleClock*    clock_    = nullptr;
    cerfengine::HostRequestChannel* requests_ = nullptr;
    Sa11xxDma*          dma_      = nullptr;
    Sa11xxUartRxLine*   line_     = nullptr;
    std::array<std::unique_ptr<Channel>, kPorts> ch_{};
    uint32_t            count_ = 0;
};
