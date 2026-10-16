#define NOMINMAX
#include "freescale_uart_sdma.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "freescale_uart_receiver.h"
#include "freescale_uart_regs.h"
#include "freescale_uart_transmitter.h"

#include <algorithm>

namespace cerfcore {

using namespace cerf_freescale_uart;

void FreescaleUartSdma::Bind(FreescaleSdmaChannelHost* host, uint32_t tx_event, uint32_t rx_event,
                             std::function<void()> on_claim) {
    if (host_ == host) return;
    host_     = host;
    tx_event_ = tx_event;
    rx_event_ = rx_event;
    on_claim_ = std::move(on_claim);
    host->RegisterChannelSink(this);
}

void FreescaleUartSdma::Reset() {
    tx_active_ = false;
    tx_open_   = false;
    rx_active_ = false;
    rx_open_   = false;
}

bool FreescaleUartSdma::HandlesEvent(int event) const {
    return host_ != nullptr &&
           (event == static_cast<int>(tx_event_) || event == static_cast<int>(rx_event_));
}

bool FreescaleUartSdma::ClaimChannel(const FreescaleSdmaChannelStart& start) {
    if (!HandlesEvent(start.event)) return false;
    const bool tx = start.event == static_cast<int>(tx_event_);
    FreescaleSdmaBdRing& ring = tx ? ring_tx_ : ring_rx_;
    ring.Begin(start);
    uint32_t wm = 0;
    const uint32_t burst = host_->ChannelWatermark(start.channel, wm) && wm != 0u ? wm : 1u;
    if (tx) {
        tx_active_ = true;
        tx_wm_     = burst;
        tx_open_   = false;
    } else {
        rx_active_ = true;
        rx_wm_     = burst;
        rx_open_   = false;
    }
    LOG(SocUart, "UART%d: SDMA %s channel %u starts at BD %08X (event %d, %u-byte bursts)\n",
        uart_, tx ? "transmit" : "receive", start.channel, ring.Current(), start.event, burst);
    on_claim_();
    return true;
}

void FreescaleUartSdma::ReleaseChannel(uint32_t channel) {
    if (tx_active_ && channel == ring_tx_.Channel()) {
        tx_active_ = false;
        tx_open_   = false;
    }
    if (rx_active_ && channel == ring_rx_.Channel()) {
        rx_active_ = false;
        rx_open_   = false;
    }
}

void FreescaleUartSdma::OnSdmaClock(bool running) {
    const bool resumed = running && !sdma_clocked_;
    sdma_clocked_ = running;
    if (resumed) on_claim_();
}

/* MCIMX51RM Table 7-32 (page 7-69): CG 01 "Clock is on in run mode, but off in WAIT and STOP
   modes"; note: "clocks to the module will be stopped immediately". */
void FreescaleUartSdma::Service(uint64_t tick, const FreescaleUartDmaEnables& en) {
    if (!sdma_clocked_) return;
    ServiceTx(tick, en);
    ServiceRx(tick, en);
}

/* MCIMX51RM page 52-231: "The SDMA script cannot process a BD with a Done bit to 0";
   Linux imx-sdma sdma_update_channel_loop: "SDMA stops cyclic channel when DMA request triggers a
   channel and no SDMA owned buffer is available". */
bool FreescaleUartSdma::Opened(FreescaleSdmaBdRing& ring, FreescaleSdmaBd& bd, bool& active,
                               const char* dir) {
    if (ring.Open(bd)) return true;
    active = false;
    host_->StopChannel(ring.Channel());
    LOG(SocUart, "UART%d: SDMA %s channel %u stops at unowned BD %08X\n", uart_, dir,
        ring.Channel(), ring.Current());
    return false;
}

void FreescaleUartSdma::Ended(FreescaleSdmaBdRing& ring, bool& active, const char* dir,
                              uint32_t bd) {
    active = false;
    LOG(SocUart, "UART%d: SDMA %s channel %u ends after BD %08X (C clear)\n", uart_, dir,
        ring.Channel(), bd);
}

bool FreescaleUartSdma::OpenTx() {
    if (tx_open_) return true;
    FreescaleSdmaBd bd;
    if (!Opened(ring_tx_, bd, tx_active_, "transmit")) return false;
    tx_buf_  = bd.buffer;
    tx_left_ = bd.count;
    tx_cont_ = bd.cont;
    tx_open_ = true;
    return true;
}

void FreescaleUartSdma::RetireTx() {
    tx_open_ = false;
    const uint32_t bd = ring_tx_.Current();
    if (!ring_tx_.Retire(*host_, tx_cont_)) Ended(ring_tx_, tx_active_, "transmit", bd);
}

/* MCIMX51RM Table 59-22 (page 59-34): dma_req_tx is TXDMAEN with TRDY; Table 59-13 TRDY: "the TxFIFO
   emptied below its target threshold". */
void FreescaleUartSdma::ServiceTx(uint64_t tick, const FreescaleUartDmaEnables& en) {
    auto& mem = emu_.Get<EmulatedMemory>();
    while (tx_active_ && en.tx && tx_.Level() <= en.txtl) {
        if (!OpenTx()) return;
        const uint32_t burst = std::min(tx_wm_, tx_left_);
        for (uint32_t i = 0; i < burst; ++i) {
            uint8_t byte = 0;
            if (mem.TryCopyOut(tx_buf_, &byte, 1u) == 0u) {
                tx_left_ = 0u;
                break;
            }
            tx_.Write(tick, byte);
            ++tx_buf_;
            --tx_left_;
        }
        if (tx_left_ == 0u) RetireTx();
    }
}

bool FreescaleUartSdma::OpenRx() {
    if (rx_open_) return true;
    FreescaleSdmaBd bd;
    if (!Opened(ring_rx_, bd, rx_active_, "receive")) return false;
    rx_bd_pa_ = bd.pa;
    rx_buf_   = bd.buffer;
    rx_cap_   = bd.count;
    rx_fill_  = 0u;
    rx_cont_  = bd.cont;
    rx_open_  = true;
    return true;
}

/* MCIMX51RM page 52-231: "The count field of the buffer descriptor indicates how much data has been
   transmitted"; Linux imx-sdma: bd->mode.count "contains the number of bytes present in the current
   buffer descriptor". */
void FreescaleUartSdma::CloseRx() {
    uint8_t* word = emu_.Get<EmulatedMemory>().TryTranslateWrite(rx_bd_pa_);
    cerf::le::Put32(word, (cerf::le::U32(word) & ~0xFFFFu) | rx_fill_);
    rx_open_ = false;
    const uint32_t bd = ring_rx_.Current();
    if (!ring_rx_.Retire(*host_, rx_cont_)) Ended(ring_rx_, rx_active_, "receive", bd);
}

/* MCIMX51RM Table 59-22 (page 59-34): dma_req_rx is RXDMAEN with RRDY, ATDMAEN with AGTIM, IDDMAEN
   with IDLE; Linux imx.c imx_uart_dma_rx_callback: "The UART script from the SDMA firmware will jump
   to the next buffer descriptor, once a DMA transaction if finalized". */
void FreescaleUartSdma::ServiceRx(uint64_t tick, const FreescaleUartDmaEnables& en) {
    auto& mem = emu_.Get<EmulatedMemory>();
    while (rx_active_) {
        const bool ready = en.rx && rx_.Level() >= en.rxtl;
        const bool aging = en.aging && (rx_.Usr1() & kUsr1Agtim) != 0u;
        const bool idle  = en.idle && (rx_.Usr2() & kUsr2Idle) != 0u;
        if (!ready && !aging && !idle) return;
        if (!OpenRx()) return;
        const uint32_t want = aging || idle ? rx_.Level() : std::min(rx_wm_, rx_.Level());
        const uint32_t limit = std::min({want, rx_cap_ - rx_fill_, kFifoDepth});
        const uint32_t room  = mem.WritableLength(rx_buf_ + rx_fill_, limit);
        if (limit != 0u && room == 0u) {
            CloseRx();
            continue;
        }
        uint8_t data[kFifoDepth];
        const uint32_t n = rx_.Take(tick, room, data);
        mem.TryCopyIn(rx_buf_ + rx_fill_, data, n);
        rx_fill_ += n;
        if (rx_fill_ == rx_cap_) {
            CloseRx();
            continue;
        }
        if (!aging && !idle) continue;
        if (rx_.Level() != 0u) continue;
        if (rx_fill_ != 0u) CloseRx();
        rx_.ClearFlags(tick, aging ? kUsr1Agtim : 0u, idle ? kUsr2Idle : 0u);
    }
}

void FreescaleUartSdma::Save(StateWriter& w) const {
    ring_tx_.Save(w, "uart_tx_");
    w.Write<uint8_t>("uart_tx_active", tx_active_ ? 1u : 0u);
    w.Write<uint8_t>("uart_tx_open", tx_open_ ? 1u : 0u);
    w.Write("uart_tx_wm", tx_wm_);
    w.Write("uart_tx_buf", tx_buf_);
    w.Write("uart_tx_left", tx_left_);
    w.Write<uint8_t>("uart_tx_cont", tx_cont_ ? 1u : 0u);
    ring_rx_.Save(w, "uart_rx_");
    w.Write<uint8_t>("uart_rx_active", rx_active_ ? 1u : 0u);
    w.Write<uint8_t>("uart_rx_open", rx_open_ ? 1u : 0u);
    w.Write("uart_rx_wm", rx_wm_);
    w.Write("uart_rx_bd_pa", rx_bd_pa_);
    w.Write("uart_rx_buf", rx_buf_);
    w.Write("uart_rx_cap", rx_cap_);
    w.Write("uart_rx_fill", rx_fill_);
    w.Write<uint8_t>("uart_rx_cont", rx_cont_ ? 1u : 0u);
}

void FreescaleUartSdma::Restore(StateReader& r) {
    uint8_t tx_active = 0, tx_open = 0, tx_cont = 0, rx_active = 0, rx_open = 0, rx_cont = 0;
    ring_tx_.Restore(r, "uart_tx_");
    r.Read("uart_tx_active", tx_active);
    r.Read("uart_tx_open", tx_open);
    r.Read("uart_tx_wm", tx_wm_);
    r.Read("uart_tx_buf", tx_buf_);
    r.Read("uart_tx_left", tx_left_);
    r.Read("uart_tx_cont", tx_cont);
    ring_rx_.Restore(r, "uart_rx_");
    r.Read("uart_rx_active", rx_active);
    r.Read("uart_rx_open", rx_open);
    r.Read("uart_rx_wm", rx_wm_);
    r.Read("uart_rx_bd_pa", rx_bd_pa_);
    r.Read("uart_rx_buf", rx_buf_);
    r.Read("uart_rx_cap", rx_cap_);
    r.Read("uart_rx_fill", rx_fill_);
    r.Read("uart_rx_cont", rx_cont);
    tx_active_ = tx_active != 0u;
    tx_open_   = tx_open != 0u;
    tx_cont_   = tx_cont != 0u;
    rx_active_ = rx_active != 0u;
    rx_open_   = rx_open != 0u;
    rx_cont_   = rx_cont != 0u;
}

}
