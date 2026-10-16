#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "sa11xx_dma_port.h"
#include "sa11xx_mcp_receive_path.h"
#include "sa11xx_mcp_sample_counter.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Sa11xxDma;
class Sa11xxDmaTransmitFifo;
class Sa11xxMcpFrameClock;
class StateReader;
class StateWriter;

class Sa11xxMcpChannel : public Service {
public:
    struct Config {
        uint32_t device_address  = 0;
        uint32_t ds_transmit     = 0;
        uint32_t ds_receive      = 0;
        uint32_t divisor_shift   = 0;
        uint32_t divisor_min     = 0;
        uint64_t low_rate_hz     = 0;
        uint32_t codec_reg       = 0;
        uint32_t transmit_enable = 0;
        uint32_t receive_enable  = 0;
        uint32_t ts = 0, rs = 0, tu = 0, ro = 0, nf = 0, ne = 0, ce = 0;
    };

    bool ShouldRegister() override;
    void OnReady() override;

    void SetLineListener(std::function<void(uint64_t now)> fn) { line_ = std::move(fn); }

    void     Settle(uint64_t now);
    void     Freeze();
    void     PrepareControl(uint64_t now, uint32_t mccr0, uint32_t mccr1);
    void     Resume(uint64_t frozen, uint64_t base);
    void     FinishControl(uint64_t now);
    bool     CodecWrite(uint64_t now, uint64_t carry, uint32_t reg, uint16_t value, bool looped);
    uint32_t Status(uint64_t now);
    void     ClearStatus(uint64_t now, uint32_t mask);
    void     WriteData(uint64_t now, uint32_t value);
    uint32_t ReadData(uint64_t now);
    bool     LineLevel() const;
    bool     NextRise(uint64_t& tick) const;

    void Save(StateWriter& w);
    void Restore(StateReader& r);

protected:
    Sa11xxMcpChannel(CerfEmulator& emu, const Config& cfg) : Service(emu), cfg_(cfg) {}

    void AttachPaths(Sa11xxDmaTransmitFifo* tx, Sa11xxMcpReceivePath* rx) {
        tx_ = tx;
        rx_ = rx;
    }

private:
    class Port : public Sa11xxDmaPort {
    public:
        Port(Sa11xxMcpChannel& c, bool receive) : c_(c), receive_(receive) {}
        uint32_t DeviceAddress() const override { return c_.cfg_.device_address; }
        bool     Receive() const override { return receive_; }
        uint32_t DatumBytes() const override { return 2u; }
        void     Settle(uint64_t now) override { c_.Settle(now); }
        void     SetSupply(uint64_t now, uint64_t words) override {
            c_.SetSupply(now, receive_, words);
        }
        uint64_t Moved() const override;
        bool     CycleOfMoved(uint64_t words, uint64_t& cycle) override {
            return receive_ ? c_.RxCycleOfMoved(words, cycle) : c_.TxCycleOfMoved(words, cycle);
        }
        GuestCycleClock::Rate WordRate() const override { return c_.WordRate(receive_); }
        bool     LoopedBack() const override { return !receive_ && c_.Looped(); }
        bool     FillReceived(uint32_t pa, uint32_t bytes) override {
            return receive_ && c_.rx_->Fill(pa, bytes);
        }

    private:
        Sa11xxMcpChannel& c_;
        const bool        receive_;
    };

    uint64_t PeriodOf(uint32_t mccr0, uint32_t mccr1) const;
    uint64_t Period() const { return period_; }
    bool     Mce() const;
    bool     Looped() const;
    bool     Adm() const;
    Sa11xxMcpReceivePath::LoopFuture Future() const;
    void     Disable();
    void     ClearFifos();
    void     SetSupply(uint64_t now, bool receive, uint64_t words);
    bool     TxCycleOfMoved(uint64_t words, uint64_t& cycle);
    bool     RxCycleOfMoved(uint64_t words, uint64_t& cycle);
    bool     TakeTick(uint64_t take, uint64_t& tick) const;
    GuestCycleClock::Rate WordRate(bool receive) const;

    const Config            cfg_;
    std::function<void(uint64_t now)> line_;
    GuestCycleClock*        clock_  = nullptr;
    Sa11xxDma*              dma_    = nullptr;
    Sa11xxMcpFrameClock*    frames_ = nullptr;
    Sa11xxDmaTransmitFifo*  tx_     = nullptr;
    Sa11xxMcpReceivePath*   rx_     = nullptr;
    Sa11xxMcpSampleCounter  counter_;
    Port                    tx_port_{*this, false};
    Port                    rx_port_{*this, true};
    bool     underrun_   = false;
    bool     was_looped_ = false;
    uint32_t mccr0_      = 0;
    uint64_t period_     = 0;
    uint16_t codec_b_    = 0;
    uint16_t decoded_b_  = 0;
};

}
