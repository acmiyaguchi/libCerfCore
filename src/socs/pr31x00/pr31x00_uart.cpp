#include "pr31x00_uart.h"

#include "pr31x00_clock.h"
#include "pr31x00_intc.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/serial/serial_cradle.h"
#include "../../peripherals/serial/serial_endpoint.h"
#include <cerfcore/state/state_stream.h>
#include "../../tracing/kernel_debug_sink.h"

#include <cstdint>
#include <utility>

namespace {

constexpr uint32_t kOffCtl1     = 0x00u;   /* $0B0 UARTA, $0C8 UARTB */
constexpr uint32_t kOffCtl2     = 0x04u;   /* write-only */
constexpr uint32_t kOffDmaCtl1  = 0x08u;   /* write-only */
constexpr uint32_t kOffDmaCtl2  = 0x0Cu;   /* write-only */
constexpr uint32_t kOffDmaCount = 0x10u;   /* read-only */
constexpr uint32_t kOffData     = 0x14u;   /* write TXHOLD, read RXHOLD */

/* UART Control 2 (§16.5.2), write-only: Baud Rate = f_UARTCLK / ((BAUDRATE + 1) * 16).
   The field is BAUDRATE[10:0] on the TMPR3912 parts and [9:0] on the TMPR3911, and the
   PR31700 carries the TMPR3912 internal function register map (R3912.H). */
constexpr uint32_t kCtl2BaudMask = 0x000007FFu;
constexpr uint64_t kClocksPerBaud = 16u;

/* UART DMA Control 1 (§16.5.3): DMASTARTVAL[31:2] is the DMA buffer's physical
   address; bits 1-0 reserved. DMA Control 2 (§16.5.4): DMALENGTH[15:0], which carries
   the buffer length MINUS ONE (§16.2.4), so serial.dll's 511 is a 512-byte buffer. */
constexpr uint32_t kDmaCtl1Reserved = 0x00000003u;
constexpr uint32_t kDmaCtl2Reserved = 0xFFFF0000u;
constexpr uint32_t kDmaLenMask      = 0x0000FFFFu;

/* Transmit Holding (§16.5.6), write-only: BREAK<8> TXDATA[7:0]. Setting BREAK with
   TXDATA $00 drives a line break until it is cleared. */
constexpr uint32_t kTxBreak        = 1u << 8;
constexpr uint32_t kTxDataMask     = 0x000000FFu;
constexpr uint32_t kTxHoldReserved = 0xFFFFFE00u;

/* UART Control 1 (§16.5.1): UARTON<31> EMPTY<30> PRXHOLDFULL<29> RXHOLDFULL<28>
   are read-only; ENUART<0> and the frame-format bits are R/W. EMPTY and DISTXD<6>
   are the only two bits that reset to 1, and UARTON reads 0 while the module is
   off - nk.exe sub_9F411310 stops the CPU only when it is. */
constexpr uint32_t kCtl1Reset = (1u << 30) | (1u << 6);

constexpr uint32_t kCtl1Uarton      = 1u << 31;
constexpr uint32_t kCtl1Empty       = 1u << 30;
constexpr uint32_t kCtl1PrxHoldFull = 1u << 29;
constexpr uint32_t kCtl1RxHoldFull  = 1u << 28;
constexpr uint32_t kCtl1Reserved    = 0x0FFF0000u;
constexpr uint32_t kCtl1DisTxd   = 1u << 6;
constexpr uint32_t kCtl1EnUart   = 1u << 0;

/* TMPR3911 §16.5.1 (pages 16-11 / 16-12): ENDMARX<15> ENDMATX<14> TESTMODE<13>
   ENBREAKHALT<12> ENDMATEST<11> ENDMALOOP<10> LOOPBACK<4>. */
constexpr uint32_t kCtl1EnDmaRx   = 1u << 15;
constexpr uint32_t kCtl1EnDmaTx   = 1u << 14;
constexpr uint32_t kCtl1EnDmaLoop = 1u << 10;
constexpr uint32_t kCtl1Unmodeled = 0x00003810u;
constexpr uint32_t kCtl1Writable  = 0x0000C7EFu;

/* Frame format (§16.5.1): BIT_7<3> selects a 7-bit character, ENPARITY<1> enables
   parity with EVENPARITY<2> selecting even, TWOSTOP<5> selects two stop bits. */
constexpr uint32_t kCtl1Bit7       = 1u << 3;
constexpr uint32_t kCtl1EvenParity = 1u << 2;
constexpr uint32_t kCtl1EnParity   = 1u << 1;
constexpr uint32_t kCtl1TwoStop    = 1u << 5;

}  /* namespace */

bool Pr31x00Uart::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00Uart::OnReady() {
    ctl1_ = kCtl1Reset & kCtl1Writable;
    module_clock_ = &emu_.Get<Pr31x00Clock>();
    dma_      = &emu_.Get<Pr31x00UartDma>();
    dma_port_ = dma_->Attach(TxSource(),
                             [this](const Pr31x00UartDma::RxInts& i) { RaiseRxInts(i); },
                             [this] { OnRxLineIdle(); });
    tx_      = &emu_.Get<Pr31x00UartTransmitter>();
    tx_port_ = tx_->Attach(dma_port_,
                           [this](const uint8_t* data, size_t n) { DeliverTx(data, n); },
                           [this](const Pr31x00UartTransmitter::TxInts& i) { RaiseTxInts(i); });
    module_clock_->RegisterModuleClockListener([this] {
        bool moved;
        {
            std::lock_guard<std::mutex> lk(mu_);
            moved = ComputeLineConfigLocked().baud != fired_baud_;
        }
        if (moved) FireLineConfig();
        else       ApplyClockState();
    });
    line_cfg_   = ComputeLineConfigLocked();
    fired_baud_ = line_cfg_.baud;
    emu_.Get<PeripheralDispatcher>().Register(this);
    /* Control 2, DMA Control 1 / 2 and DMA Count RESET X (TMPR3911 §16.5.2-16.5.5 p16-14,
       p16-15). */
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) {
        { std::lock_guard<std::mutex> lk(mu_); ctl1_ = kCtl1Reset & kCtl1Writable; }
        dma_->ResetLine(dma_port_);
        tx_->ResetLine(tx_port_);
    });
    reset.RegisterResetReleaseListener([this] { FireLineConfig(); });

    if (auto* wiring = emu_.TryGet<Pr31x00SerialWiring>()) modem_ = wiring->ForUart(MmioBase());
    modem_lines_ = &emu_.Get<Pr31x00UartModemLines>();
    modem_port_  = modem_lines_->Attach(modem_ ? &*modem_ : nullptr,
                                        [this](bool dtr, bool rts) { NotifyControlLines(dtr, rts); });
    if (!modem_) return;

    /* No endpoint is attached yet. The lines are active low, so leaving their pins
       at the 0 they reset to reads as CTS and carrier already asserted - on the Velo
       that also fires the driver's carrier-detect system event (sub_1EB2750). */
    SetModemInputs(false, false, false, false);

    cradle_ = std::make_unique<SerialCradle>(emu_, *this, modem_->label);
    emu_.Get<HostWidgetRegistry>().Register(cradle_.get());
    LOG(Periph, "[UART] %s attached as serial endpoint line '%ls'\n",
        TxSource(), modem_->label.c_str());
}

Pr31x00Uart::Pr31x00Uart(CerfEmulator& emu) : Peripheral(emu) {}

Pr31x00Uart::~Pr31x00Uart() = default;

void Pr31x00Uart::OnShutdown() {
    if (cradle_) cradle_->OnShutdown();
}

uint32_t Pr31x00Uart::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        /* TMPR3911 §16.5.1 (pages 16-11 / 16-12): UARTON "the module will not shut down until
           the Transmit Holding Register and Transmit Shift Register are empty"; EMPTY "is high
           if the Transmit Holding Register and Transmit Shift Register are both empty". */
        case kOffCtl1: {
            uint32_t v = 0;
            if (dma_->PrxHoldFull(dma_port_)) v |= kCtl1PrxHoldFull;
            if (dma_->RxHoldFull(dma_port_))  v |= kCtl1RxHoldFull;
            const bool tx_empty = tx_->Empty(tx_port_);
            std::lock_guard<std::mutex> lk(mu_);
            v |= ctl1_ & kCtl1Writable;
            if (tx_empty) v |= kCtl1Empty;
            if ((ctl1_ & kCtl1EnUart) || !tx_empty) v |= kCtl1Uarton;
            return v;
        }

        /* DMA_COUNT (§16.5.5), read-only: the receive DMA's own address counter. The
           driver adds it to the buffer base to find the write offset and copies up to
           there (serial.dll sub_1EB2620), keeping its read cursor privately. */
        case kOffDmaCount: return dma_->Count(dma_port_);

        default: HaltUnsupportedAccess("PR31x00 UART ReadWord", addr, 0);
    }
}

void Pr31x00Uart::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
        case kOffCtl1: WriteCtl1(addr, value); return;

        case kOffCtl2:
            { std::lock_guard<std::mutex> lk(mu_); ctl2_baud_ = value & kCtl2BaudMask; }
            FireLineConfig();
            return;

        case kOffDmaCtl1:
            if (value & kDmaCtl1Reserved) {
                HaltUnsupportedAccess("PR31x00 UART DMA_CTL1 reserved", addr, value);
            }
            dma_->SetBuffer(dma_port_, value);
            return;

        case kOffDmaCtl2:
            if (value & kDmaCtl2Reserved) {
                HaltUnsupportedAccess("PR31x00 UART DMA_CTL2 reserved", addr, value);
            }
            dma_->SetLength(dma_port_, (value & kDmaLenMask) + 1u);
            return;

        case kOffData: WriteTxHold(addr, value); return;

        default: HaltUnsupportedAccess("PR31x00 UART WriteWord", addr, value);
    }
}

void Pr31x00Uart::WriteCtl1(uint32_t addr, uint32_t value) {
    if (value & kCtl1Reserved) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 reserved", addr, value);
    }
    if (value & kCtl1Unmodeled) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 unmodeled control", addr, value);
    }
    if ((value & kCtl1EnDmaRx) && (value & kCtl1EnDmaTx)) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 sets ENDMARX and ENDMATX", addr, value);
    }
    if ((value & kCtl1EnDmaRx) && !(value & kCtl1EnDmaLoop)) {
        HaltUnsupportedAccess("PR31x00 UART RX DMA armed without ENDMALOOP", addr, value);
    }
    if ((value & kCtl1EnDmaTx) && (value & kCtl1EnDmaLoop)) {
        HaltUnsupportedAccess("PR31x00 UART TX DMA armed with ENDMALOOP", addr, value);
    }
    bool tx_start;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tx_start = (value & kCtl1EnDmaTx) != 0u && (ctl1_ & kCtl1EnDmaTx) == 0u;
        ctl1_    = value & kCtl1Writable;
    }
    if ((value & kCtl1EnDmaTx) == 0u) tx_->CancelDma(tx_port_);
    dma_->SetControl(dma_port_, (value & kCtl1EnUart) != 0u, (value & kCtl1EnDmaRx) != 0u);
    FireLineConfig();
    if (tx_start) TransmitDma(addr, value);
}

/* TMPR3911 §16.5.1 (page 16-12) ENDMATX "should not be set until the UARTDMACNTL1 and
   UARTDMACNTL2 registers are setup, the module is enabled and the empty flag is set". */
void Pr31x00Uart::TransmitDma(uint32_t addr, uint32_t control) {
    if ((control & kCtl1EnUart) == 0u || (control & kCtl1DisTxd) != 0u) {
        HaltUnsupportedAccess("PR31x00 UART TX DMA armed with ENUART clear or DISTXD set",
                              addr, control);
    }
    if (dma_->Length(dma_port_) == 0u) {
        HaltUnsupportedAccess("PR31x00 UART TX DMA armed with no DMALENGTH programmed",
                              addr, control);
    }
    tx_->StartDma(tx_port_);
}

void Pr31x00Uart::RaiseTxInts(const Pr31x00UartTransmitter::TxInts& ints) {
    uint32_t bits = 0;
    if (ints.sent)     bits |= TxEmptyIntBit() | TxAvailIntBit();
    if (ints.dma_full) bits |= DmaFullIntBit();
    if (ints.dma_half) bits |= DmaHalfIntBit();
    emu_.Get<Pr31x00Intc>().SetPending(1, bits);
}

void Pr31x00Uart::DeliverTx(const uint8_t* data, size_t n) {
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    if (endpoint_) {
        endpoint_->OnGuestTx(data, n);
        if (cradle_) cradle_->MarkTx();
        return;
    }
    auto& sink = emu_.Get<KernelDebugSink>();
    for (size_t i = 0; i < n; ++i) sink.EmitChar(static_cast<char>(data[i]), tx_line_, TxSource());
}

void Pr31x00Uart::WriteTxHold(uint32_t addr, uint32_t value) {
    if (value & kTxHoldReserved) {
        HaltUnsupportedAccess("PR31x00 UART TXHOLD reserved", addr, value);
    }
    if (value & kTxBreak) {
        HaltUnsupportedAccess("PR31x00 UART TXHOLD BREAK", addr, value);
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        if ((ctl1_ & kCtl1EnUart) == 0u || (ctl1_ & kCtl1DisTxd) != 0u) {
            return;
        }
    }

    tx_->WriteHold(tx_port_, static_cast<uint8_t>(value & kTxDataMask));
}

void Pr31x00Uart::PushRx(const uint8_t* data, size_t n) {
    if (n == 0) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if ((ctl1_ & kCtl1EnUart) == 0u) return;
    }
    dma_->Receive(dma_port_, data, n);
    if (cradle_) cradle_->MarkRx();
}

/* The wire, not the guest's buffer: the DMA engine cannot tell how far the driver has
   read, so a feeder is free to send again once its bytes have all been clocked out. */
bool Pr31x00Uart::RxEmpty() const {
    return dma_->LineIdle(dma_port_);
}

void Pr31x00Uart::OnRxLineIdle() {
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    if (rx_drain_cb_) rx_drain_cb_();
}

void Pr31x00Uart::RaiseRxInts(const Pr31x00UartDma::RxInts& ints) {
    uint32_t bits = 0;
    if (ints.rx)       bits |= RxIntBit();
    if (ints.overrun)  bits |= RxOverrunIntBit();
    if (ints.dma_half) bits |= DmaHalfIntBit();
    if (ints.dma_full) bits |= DmaFullIntBit();
    emu_.Get<Pr31x00Intc>().SetPending(1, bits);
}

void Pr31x00Uart::SetRxDrainCallback(RxDrainFn cb) {
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    rx_drain_cb_ = std::move(cb);
}

void Pr31x00Uart::SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) {
    if (modem_) modem_lines_->SetInputs(modem_port_, cts, dsr, ri, dcd);
}

SerialLine::LineConfig Pr31x00Uart::ComputeLineConfigLocked() const {
    LineConfig c;
    const GuestCycleClock::Rate uartclk = module_clock_->UartClockRate(ClockEnableBit());
    c.baud      = static_cast<uint32_t>(
        uartclk.num / (uartclk.den * kClocksPerBaud * (static_cast<uint64_t>(ctl2_baud_) + 1u)));
    c.data_bits = (ctl1_ & kCtl1Bit7) ? 7u : 8u;
    if ((ctl1_ & kCtl1EnParity) == 0u) c.parity = LineConfig::Parity::None;
    else c.parity = (ctl1_ & kCtl1EvenParity) ? LineConfig::Parity::Even
                                              : LineConfig::Parity::Odd;
    c.stop = (ctl1_ & kCtl1TwoStop) ? LineConfig::Stop::Two : LineConfig::Stop::One;
    return c;
}

bool Pr31x00Uart::UartClockRunning() const {
    return module_clock_->UartClockRate(ClockEnableBit()).num != 0u;
}

void Pr31x00Uart::ApplyClockState() {
    const bool running = UartClockRunning();
    dma_->SetClockRunning(dma_port_, running);
    tx_->SetClockRunning(tx_port_, running);
}

SerialLine::LineConfig Pr31x00Uart::GetLineConfig() const {
    std::lock_guard<std::mutex> lk(mu_);
    return line_cfg_;
}

void Pr31x00Uart::SetLineConfigCallback(LineConfigFn cb) {
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    line_cfg_cb_ = std::move(cb);
}

void Pr31x00Uart::FireLineConfig() {
    LineConfig cfg;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cfg         = ComputeLineConfigLocked();
        fired_baud_ = cfg.baud;
        line_cfg_   = cfg;
    }
    ApplyClockState();
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    if (line_cfg_cb_) line_cfg_cb_(cfg);
}

void Pr31x00Uart::SetEndpoint(SerialEndpoint* ep) {
    bool dtr, rts;
    modem_lines_->Control(modem_port_, dtr, rts);
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    endpoint_ = ep;
    if (ep) ep->OnControlLines(dtr, rts);
}

void Pr31x00Uart::NotifyControlLines(bool dtr, bool rts) {
    std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
    if (endpoint_) endpoint_->OnControlLines(dtr, rts);
}

/* Every insert and eject takes SerialCradle::mtx_ and then this mu_ (SetEndpoint,
   GetLineConfig), so mu_ is dropped before the cradle is asked for its own state:
   holding it across that call is the opposite order and deadlocks a save against a
   user ejecting the card. */
void Pr31x00Uart::SaveState(StateWriter& w) {
    /* Not named ctl1: dlgs.h (via windows.h) defines that as a dialog control id. */
    uint32_t ctl1_v, baud;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ctl1_v = ctl1_;
        baud   = ctl2_baud_;
    }
    w.Write("ctl1_v", ctl1_v);
    w.Write("baud", baud);
    modem_lines_->SaveState(modem_port_, w);
    dma_->SaveState(dma_port_, w);
    tx_->SaveState(tx_port_, w);
    if (cradle_) cradle_->SaveCradleState(w);
}

void Pr31x00Uart::RestoreState(StateReader& r) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        r.Read("ctl1_v", ctl1_);
        r.Read("baud", ctl2_baud_);
    }
    modem_lines_->RestoreState(modem_port_, r);
    {
        std::lock_guard<std::recursive_mutex> elk(endpoint_mu_);
        endpoint_ = nullptr;
        rx_drain_cb_ = nullptr;
        line_cfg_cb_ = nullptr;
    }
    dma_->RestoreState(dma_port_, r);
    tx_->RestoreState(tx_port_, r);
    if (cradle_) cradle_->RestoreCradleState(r);
}

void Pr31x00Uart::PostRestore() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        line_cfg_   = ComputeLineConfigLocked();
        fired_baud_ = line_cfg_.baud;
    }
    ApplyClockState();
    if (cradle_) cradle_->PostRestore();
}
