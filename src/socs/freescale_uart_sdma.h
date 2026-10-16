#pragma once

#include "freescale_sdma_bd_ring.h"
#include "freescale_sdma_bus.h"

#include <cstdint>
#include <functional>

class CerfEmulator;
class FreescaleUartReceiver;
class FreescaleUartTransmitter;
class StateReader;
class StateWriter;

struct FreescaleUartDmaEnables {
    bool     tx    = false;
    bool     rx    = false;
    bool     aging = false;
    bool     idle  = false;
    uint32_t txtl  = 0;
    uint32_t rxtl  = 1;
};

class FreescaleUartSdma : public FreescaleSdmaChannelSink {
public:
    FreescaleUartSdma(CerfEmulator& emu, int uart, FreescaleUartTransmitter& tx,
                      FreescaleUartReceiver& rx)
        : emu_(emu), uart_(uart), tx_(tx), rx_(rx), ring_tx_(emu), ring_rx_(emu) {}

    void Bind(FreescaleSdmaChannelHost* host, uint32_t tx_event, uint32_t rx_event,
              std::function<void()> on_claim);
    void Reset();
    void Service(uint64_t tick, const FreescaleUartDmaEnables& en);

    bool HandlesEvent(int event) const override;
    bool ClaimChannel(const FreescaleSdmaChannelStart& start) override;
    void ReleaseChannel(uint32_t channel) override;
    void OnSdmaClock(bool running) override;
    void SaveSinkState(StateWriter&) override {}
    void RestoreSinkState(StateReader&) override {}
    void PostRestoreSink() override {}

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    void ServiceTx(uint64_t tick, const FreescaleUartDmaEnables& en);
    void ServiceRx(uint64_t tick, const FreescaleUartDmaEnables& en);
    bool OpenTx();
    bool OpenRx();
    void RetireTx();
    void CloseRx();
    bool Opened(FreescaleSdmaBdRing& ring, FreescaleSdmaBd& bd, bool& active, const char* dir);
    void Ended(FreescaleSdmaBdRing& ring, bool& active, const char* dir, uint32_t bd);

    CerfEmulator&             emu_;
    int                       uart_;
    FreescaleUartTransmitter& tx_;
    FreescaleUartReceiver&    rx_;
    FreescaleSdmaChannelHost* host_     = nullptr;
    uint32_t                  tx_event_ = 0;
    uint32_t                  rx_event_ = 0;
    std::function<void()>     on_claim_;
    bool                      sdma_clocked_ = true;

    FreescaleSdmaBdRing ring_tx_;
    bool     tx_active_ = false;
    bool     tx_open_   = false;
    uint32_t tx_wm_     = 1;
    uint32_t tx_buf_    = 0;
    uint32_t tx_left_   = 0;
    bool     tx_cont_   = false;

    FreescaleSdmaBdRing ring_rx_;
    bool     rx_active_ = false;
    uint32_t rx_wm_     = 1;
    bool     rx_open_   = false;
    uint32_t rx_bd_pa_  = 0;
    uint32_t rx_buf_    = 0;
    uint32_t rx_cap_    = 0;
    uint32_t rx_fill_   = 0;
    bool     rx_cont_   = false;
};
