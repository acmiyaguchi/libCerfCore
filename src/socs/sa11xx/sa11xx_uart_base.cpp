#include "sa11xx_uart_base.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../tracing/kernel_debug_sink.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../uart_endpoint.h"
#include "sa11xx_external_clocks.h"
#include "sa11xx_intc.h"
#include "sa11xx_uart_receiver.h"
#include "sa11xx_uart_regs.h"
#include "sa11xx_uart_transmitter.h"

#include <string>

namespace cerfcore {

using namespace sa11xx_uart;

namespace {

/* SA-1110 §11.11.7 UTSR0 (printed 11-122): TFS 0; §11.11.8 UTSR1 (printed 11-125): TNF 2. */
constexpr uint32_t kUtsr0Tfs = 1u << 0;
constexpr uint32_t kUtsr1Tnf = 1u << 2;

}

bool Sa11xxUartBase::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxUartBase::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    ext_   = &emu_.Get<Sa11xxExternalClocks>();
    tx_    = &emu_.Get<Sa11xxUartTransmitter>();
    rx_    = &emu_.Get<Sa11xxUartReceiver>();
    rx_port_ = rx_->Attach(this, MmioBase() + 0x14u, ReceiveDeviceSelect(), ChannelName(),
                           ReceiveFifo());
    port_    = tx_->Attach(this, MmioBase() + 0x14u, TransmitDeviceSelect(), rx_port_,
                           TransmitFifo());
    if (SampleClockPin() >= 0) ext_->RegisterChangeListener([this] { OnClockChange(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { OnResetLine(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* §11.11.3.5 (printed 11-113): "When SCE=1, a clock is input from a GPIO pin and is used to
   synchronously drive both the transmit and receive logic"; "Serial port 2 does not support the
   sample clock function". */
bool Sa11xxUartBase::BitClock(const Control& ctl, GuestCycleClock::Rate& rate) const {
    const int pin = SampleClockPin();
    if ((ctl.utcr0 & kUtcr0Sce) == 0u || pin < 0) {
        rate = BitRate(ctl.utcr1, ctl.utcr2);
        return true;
    }
    return ext_->Rate(static_cast<uint32_t>(pin), rate);
}

void Sa11xxUartBase::OnClockChange() {
    std::vector<uint8_t> out;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        tx_->ClockChanged(port_, out);
        rx_->ClockChanged(rx_port_, clock_->Cycles());
        RefreshIrqLocked();
    }
    Emit(out);
    tx_->NotifyDma();
}

uint64_t Sa11xxUartBase::DmaMovedLocked() const {
    return tx_->DmaMoved(port_) + rx_->DmaMoved(rx_port_);
}

void Sa11xxUartBase::FlushLine() {
    std::string ascii;
    for (uint8_t b : tx_line_) {
        ascii.push_back((b >= 0x20 && b < 0x7F) ? char(b) : '.');
    }
    emu_.Get<KernelDebugSink>().EmitLine(ascii, ChannelName());
    tx_line_.clear();
}

void Sa11xxUartBase::TxByte(uint8_t b) {
    tx_line_.push_back(b);
    if (b == '\n' || tx_line_.size() >= 256) FlushLine();
    if (endpoint_ != nullptr) endpoint_->OnGuestTx(b);
}

void Sa11xxUartBase::Emit(const std::vector<uint8_t>& out) {
    for (uint8_t b : out) TxByte(b);
}

void Sa11xxUartBase::OnTransmitted(const std::vector<uint8_t>& out) {
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        RefreshIrqLocked();
    }
    Emit(out);
}

void Sa11xxUartBase::OnReceived() {
    std::lock_guard<std::mutex> lk(state_mtx_);
    RefreshIrqLocked();
}

uint32_t Sa11xxUartBase::Utsr1Locked() const {
    uint32_t v = rx_->Utsr1(rx_port_);
    if (tx_->Tnf(port_)) v |= kUtsr1Tnf;
    return v;
}

uint32_t Sa11xxUartBase::ComputeUtsr0Locked() const {
    return rx_->Utsr0(rx_port_) | (tx_->Tfs(port_) ? kUtsr0Tfs : 0u);
}

/* §11.11.5.4 / §11.11.5.5 (printed 11-117): RIE gates RFS and RID, TIE gates TFS, toward the
   interrupt controller. */
void Sa11xxUartBase::RefreshIrqLocked() {
    const int bit = IntcSourceBit();
    if (bit < 0) return;
    const bool rx_irq = rx_->InterruptRequest(rx_port_);
    const bool tx_irq = (utcr3_ & kUtcr3Tie) && tx_->Tfs(port_);
    const bool want = rx_irq || tx_irq;
    if (want && !intc_asserted_) {
        intc_asserted_ = true;
        emu_.Get<Sa11xxIntc>().AssertSource(static_cast<uint32_t>(bit));
    } else if (!want && intc_asserted_) {
        intc_asserted_ = false;
        emu_.Get<Sa11xxIntc>().DeassertSource(static_cast<uint32_t>(bit));
    }
}

void Sa11xxUartBase::AttachDevice(UartLineDevice* device) {
    if (device_ != nullptr) emu_.Get<Fatal>().Die("%s: a second line device attached", ChannelName());
    device_ = device;
}

void Sa11xxUartBase::AttachEndpoint(UartEndpoint* endpoint) {
    AttachDevice(endpoint);
    endpoint_ = endpoint;
}

void Sa11xxUartBase::PushRxBurst(const uint8_t* data, size_t n) {
    rx_->Push(rx_port_, data, n);
}

void Sa11xxUartBase::PushRxBurstNow(const uint8_t* data, size_t n) {
    rx_->PushNow(rx_port_, data, n);
}

/* §11.11.1 (printed 11-109): "Following hardware reset, the UART is disabled"; "Reset also causes
   the UART's transmit and receive FIFOs to be flushed"; UTCR3 reset row: RXE 0, TXE 0. */
void Sa11xxUartBase::OnResetLine() {
    std::lock_guard<std::mutex> lk(state_mtx_);
    utcr3_ &= ~(kUtcr3Rxe | kUtcr3Txe);
    tx_->Reset(port_);
    rx_->Reset(rx_port_);
    RefreshIrqLocked();
}

uint32_t Sa11xxUartBase::ReadReg(uint32_t off) {
    std::vector<uint8_t> out;
    uint32_t v = 0;
    bool moved = false;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        const uint64_t now    = clock_->Cycles();
        const uint64_t before = DmaMovedLocked();
        tx_->Settle(port_, out);
        rx_->Settle(rx_port_, now);
        switch (off) {
            case 0x00: v = utcr0_; break;
            case 0x04: v = utcr1_; break;
            case 0x08: v = utcr2_; break;
            case 0x0C: v = utcr3_; break;
            case 0x10: v = utcr4_; break;
            case 0x14: v = rx_->Pop(rx_port_, now); break;
            case 0x1C: v = ComputeUtsr0Locked(); break;
            case 0x20: v = Utsr1Locked(); break;
            default:   break;
        }
        RefreshIrqLocked();
        const bool replan = rx_->TakeReplan(rx_port_);
        moved = DmaMovedLocked() != before || replan;
    }
    Emit(out);
    if (moved) tx_->NotifyDma();
    return v;
}

void Sa11xxUartBase::WriteReg(uint32_t off, uint32_t value) {
    std::vector<uint8_t> out;
    bool port_change = false;
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        const uint64_t now    = clock_->Cycles();
        const uint64_t before = DmaMovedLocked();
        switch (off) {
            case 0x00: utcr0_ = value; break;
            case 0x04: utcr1_ = value; break;
            case 0x08: utcr2_ = value; break;
            case 0x0C: utcr3_ = value; break;
            case 0x10: utcr4_ = value; break;
            case 0x14: tx_->Write(port_, static_cast<uint8_t>(value & 0xFFu), out); break;
            case 0x1C: rx_->ClearStatus(rx_port_, now, value); break;
            default:   break;
        }
        if (off <= 0x0C) {
            rx_->WriteControl(rx_port_, now,
                              Sa11xxUartReceiver::Control{utcr0_, utcr1_, utcr2_, utcr3_});
            port_change = tx_->WriteControl(
                port_, Sa11xxUartTransmitter::Control{utcr0_, utcr1_, utcr2_, utcr3_}, out);
            tx_->Settle(port_, out);
        }
        RefreshIrqLocked();
        const bool replan = rx_->TakeReplan(rx_port_);
        port_change = port_change || DmaMovedLocked() != before || replan;
    }
    Emit(out);
    if (port_change) tx_->NotifyDma();
}

uint8_t Sa11xxUartBase::ReadByte(uint32_t addr) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (!IsKnown(base)) HaltUnsupportedAccess("ReadByte", addr, 0);
    return static_cast<uint8_t>((ReadReg(base) >> shift) & 0xFFu);
}

uint32_t Sa11xxUartBase::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (!IsKnown(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
    return ReadReg(off);
}

void Sa11xxUartBase::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off   = addr - MmioBase();
    const uint32_t base  = off & ~0x3u;
    const uint32_t shift = (off & 0x3u) * 8;
    if (!IsKnown(base)) HaltUnsupportedAccess("WriteByte", addr, value);
    if (base == 0x14) { WriteReg(base, value); return; }
    const uint32_t cur     = ReadReg(base);
    const uint32_t cleared = cur & ~(0xFFu << shift);
    WriteReg(base, cleared | (static_cast<uint32_t>(value) << shift));
}

void Sa11xxUartBase::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (!IsKnown(off)) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteReg(off, value);
}

void Sa11xxUartBase::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mtx_);
    w.Write("utcr0", utcr0_);
    w.Write("utcr1", utcr1_);
    w.Write("utcr2", utcr2_);
    w.Write("utcr3", utcr3_);
    w.Write("utcr4", utcr4_);
    w.Write("intc_asserted", intc_asserted_);
    tx_->Save(port_, w);
    rx_->Save(rx_port_, w);
    if (device_ != nullptr) device_->SaveState(w);
}

void Sa11xxUartBase::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mtx_);
    r.Read("utcr0", utcr0_);
    r.Read("utcr1", utcr1_);
    r.Read("utcr2", utcr2_);
    r.Read("utcr3", utcr3_);
    r.Read("utcr4", utcr4_);
    r.Read("intc_asserted", intc_asserted_);
    tx_->Restore(port_, r);
    rx_->Restore(rx_port_, r);
    if (device_ != nullptr) device_->RestoreState(r);
}

void Sa11xxUartBase::PostRestore() {
    ext_->Resync();
    std::lock_guard<std::mutex> lk(state_mtx_);
    rx_->PostRestore(rx_port_);
    RefreshIrqLocked();
}

}
