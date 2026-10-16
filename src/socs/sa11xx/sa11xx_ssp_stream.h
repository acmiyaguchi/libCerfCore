#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../serial_shifter_run.h"
#include "sa11xx_dma_port.h"
#include "sa11xx_ssp_frame_format.h"

#include <cstdint>
#include <deque>

namespace cerfcore {

class Sa11xxDma;
class Sa11xxGpio;
class Sa11xxIntc;
class Sa11xxMcp;
class Sa11xxPpc;
class Sa11xxSspBitClock;
class Sa11xxSspDevice;
class Sa11xxSspReceiveFifo;
class Sa11xxSspTransmitFifo;
class StateReader;
class StateWriter;

class Sa11xxSspStream : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void     WriteControl(uint64_t now, uint32_t sscr0, uint32_t sscr1);
    void     WriteData(uint64_t now, uint16_t value);
    uint16_t ReadData(uint64_t now);
    uint32_t Status(uint64_t now);
    void     ClearOverrun(uint64_t now);
    void     RefreshLine(uint64_t now);

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore(uint64_t now);

private:
    class Port : public Sa11xxDmaPort {
    public:
        Port(Sa11xxSspStream& s, bool receive) : s_(s), receive_(receive) {}
        uint32_t DeviceAddress() const override { return 0x81C01Bu; }
        bool     Receive() const override { return receive_; }
        uint32_t DatumBytes() const override { return 2u; }
        void     Settle(uint64_t now) override { s_.Settle(now); }
        void     SetSupply(uint64_t now, uint64_t words) override {
            s_.SetSupply(now, receive_, words);
        }
        uint64_t Moved() const override { return s_.MovedOf(receive_); }
        bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override {
            return receive_ ? s_.RxCycleOfMoved(words, cycle) : s_.TxCycleOfMoved(words, cycle);
        }
        GuestCycleClock::Rate WordRate() const override { return s_.WordRate(); }
        bool     LoopedBack() const override { return !receive_ && s_.Looped(); }
        bool     FillReceived(uint32_t pa, uint32_t bytes) override {
            return receive_ && s_.FillReceived(pa, bytes);
        }

    private:
        Sa11xxSspStream& s_;
        const bool       receive_;
    };

    static constexpr uint64_t kNever = UINT64_MAX;

    struct Frame {
        uint64_t load  = 0;
        uint64_t push  = kNever;
        uint16_t value = 0;
    };

    struct PinSnapshot {
        bool     reassigned = false;
        uint32_t gafr       = 0;
        uint32_t gpdr       = 0;
        uint32_t latch      = 0;
        bool operator==(const PinSnapshot&) const = default;
    };

    void     BeforeClockChange(uint64_t now);
    void     AfterClockChange(uint64_t now);
    bool     OperatingWith(uint32_t sscr0) const;
    void     ApplyEnable(uint64_t now);
    void     OnPinOwnerChange();
    void     OnPinsChange();
    PinSnapshot ReadPins() const;
    bool     Drives(uint32_t pin) const;
    bool     DeviceClocked() const;
    int32_t  PinWord(uint32_t pin, uint32_t bits) const;
    void     Disable();
    void     Settle(uint64_t now);
    bool     Instant() const;
    uint64_t DoneHalf() const;
    void     SettleLoads(uint64_t half);
    void     SettlePushes(uint64_t half);
    void     EndRun();
    int32_t  Received(uint16_t sent) const;
    void     StartRun(uint64_t now);
    void     SetSupply(uint64_t now, bool receive, uint64_t words);
    uint64_t MovedOf(bool receive) const;
    bool     FillReceived(uint32_t pa, uint32_t bytes);
    bool     Looped() const;
    bool     TxCycleOfMoved(uint64_t words, uint64_t& cycle);
    bool     RxCycleOfMoved(uint64_t words, uint64_t& cycle);
    bool     PushTick(uint64_t m, uint64_t& tick) const;
    bool     LineLevel() const;
    bool     NextRise(uint64_t& cycle) const;
    GuestCycleClock::Rate WordRate() const;

    GuestCycleClock*        clock_   = nullptr;
    GuestCycleClock::Event* line_ev_ = nullptr;
    Sa11xxIntc*             intc_    = nullptr;
    Sa11xxDma*              dma_     = nullptr;
    Sa11xxGpio*             gpio_    = nullptr;
    Sa11xxMcp*              mcp_     = nullptr;
    Sa11xxPpc*              ppc_     = nullptr;
    Sa11xxSspBitClock*      bit_     = nullptr;
    Sa11xxSspDevice*        device_  = nullptr;
    Sa11xxSspTransmitFifo*  tx_      = nullptr;
    Sa11xxSspReceiveFifo*   rx_      = nullptr;
    SerialShifterRun        run_;
    Sa11xxSspFrameFormat    fmt_;
    std::deque<Frame>       frames_;
    PinSnapshot             pins_;
    Port                    tx_port_{*this, false};
    Port                    rx_port_{*this, true};
    uint32_t sscr0_   = 0;
    uint32_t sscr1_   = 0;
    bool     enabled_ = false;
    uint16_t last_rx_ = 0;
};

}
