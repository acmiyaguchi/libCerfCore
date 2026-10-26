#include "imx28_i2c.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../i2c_slave.h"
#include "imx28_apbx.h"
#include "imx28_i2c_regs.h"
#include "imx28_icoll.h"
#include "imx28_id.h"

namespace cerfcore {

using namespace imx28_i2c;

bool Imx28I2c::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28I2c::OnReady() {
    icoll_ = &emu_.Get<Imx28Icoll>();
    if (board_slave_) master_.SetSlave(emu_.TryGet<I2cSlave>());
    emu_.Get<Imx28Apbx>().AttachDevice(dma_channel_, this);
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Imx28I2c::PeekLanes(uint32_t word_addr, uint32_t lanes) {
    const uint32_t reg = (word_addr - base_) & ~0xFu;
    return reg == kData || reg == kQueueData ? 0u : ReadLanes(word_addr, lanes);
}

void Imx28I2c::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 %08X: I2C %s at %08X (value %08X, CTRL0 %08X CTRL1 %08X)", base_,
                          what, addr, value, ctrl0_, ctrl1_);
}

void Imx28I2c::ResetBlock() {
    ctrl0_   = kCtrl0Reset;
    timing0_ = kTiming0Reset;
    timing1_ = kTiming1Reset;
    timing2_ = kTiming2Reset;
    ctrl1_   = kCtrl1Reset;
    debug0_  = kDebug0Reset;
    dma_armed_ = false;
    pio_       = Pio::Idle;
    pio_pos_   = 0u;
    tx_held_   = false;
    got_nak_   = false;
    pio_buf_.clear();
    master_.Reset();
    DriveLine(false);
}

/* Section 27.2.1: interrupt request is a CTRL1 status bit ANDed with its enable bit. */
void Imx28I2c::DriveLine(bool force) {
    const bool on = (ctrl1_ & (ctrl1_ >> kCtrl1EnableShift) & kCtrl1StatusMask) != 0u;
    if (!force && on == line_) return;
    line_ = on;
    if (on) icoll_->AssertSource(error_irq_);
    else    icoll_->DeassertSource(error_irq_);
}

/* Section 27.5.6 STAT: [7:0] are the CTRL1 status bits ANDed with their enables. */
uint32_t Imx28I2c::ComposeStat() const {
    const uint32_t summary = ctrl1_ & (ctrl1_ >> kCtrl1EnableShift) & kCtrl1StatusMask;
    uint32_t stat = kStatPresent | summary;
    if (summary != 0u)                  stat |= kStatAnyIrq;
    if (master_.BusBusy())              stat |= kStatBusBusy;
    if (got_nak_)                       stat |= kStatGotNak;
    if (pio_ == Pio::Collect)           stat |= kStatDmaWait;
    if ((ctrl0_ & kCtrl0Run) != 0u)     stat |= kStatEngineBusy;
    return stat;
}

Imx28I2cMaster::Command Imx28I2c::Decode() const {
    return Imx28I2cMaster::Command{(ctrl0_ & kCtrl0PreStart) != 0u,
                                   (ctrl0_ & kCtrl0PostStop) != 0u, ctrl0_ & kCtrl0CountMask};
}

/* Section 27.5.1 RSVD2, PRE_ACK, ACKNOWLEDGE, RSVD1, MULTI_MASTER, SLAVE_ADDRESS_ENABLE. */
void Imx28I2c::CheckCtrl0(uint32_t value, uint32_t addr) const {
    if ((value & kCtrl0Unsupported) != 0u) Die("CTRL0 bits", addr, value);
}

void Imx28I2c::WriteCtrl0(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl0_;
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        ResetBlock();
        return;
    }
    CheckCtrl0(next, addr);
    if ((next & kCtrl0ClockHeld) != 0u && (old & kCtrl0ClockHeld) == 0u) {
        Die("CLOCK_HELD set by software", addr, value);
    }
    if (pio_ != Pio::Idle || dma_armed_) Die("CTRL0 write with a command running", addr, value);
    ctrl0_ = next;
    if ((old & kCtrl0Run) == 0u && (next & kCtrl0Run) != 0u) StartPio(addr);
}

void Imx28I2c::WriteTiming(uint32_t& reg, Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, reg, value);
    if ((next & ~kTimingMask) != 0u) Die("timing register bits", addr, value);
    reg = next;
}

/* Section 27.5.5 CLR_GOT_A_NAK returns the halted controller to idle. */
void Imx28I2c::WriteCtrl1(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, ctrl1_, value & ~kCtrl1ClrGotNak);
    if ((next & kCtrl1Unsupported) != 0u) Die("CTRL1 bits", addr, value);
    if (op != Imx28SctOp::Clr && (value & kCtrl1ClrGotNak) != 0u && got_nak_) {
        got_nak_ = false;
        ctrl0_  &= ~(kCtrl0Run | kCtrl0PreStart | kCtrl0PostStop | kCtrl0CountMask);
    }
    ctrl1_ = next;
    DriveLine(false);
}

void Imx28I2c::WriteDebug0(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, debug0_, value);
    if (((next ^ debug0_) & ~kDebug0DmaReq) != 0u) Die("DEBUG0 bits", addr, value);
    debug0_ = next;
}

/* Section 27.2.3.1 Figure 27-9: the DMA PIO word is the CTRL0 value of the command. */
void Imx28I2c::DmaPioWrite(uint32_t, uint32_t index, uint32_t value) {
    const uint32_t addr = base_ + index * kWordBytes;
    if (index != 0u) Die("DMA PIO word index", addr, value);
    RequireOperating("DmaPioWrite", ctrl0_, addr, value);
    if (pio_ != Pio::Idle || dma_armed_) Die("DMA PIO word with a command running", addr, value);
    if (Gated(value)) Die("DMA PIO word with SFTRST or CLKGATE", addr, value);
    CheckCtrl0(value, addr);
    if ((value & kCtrl0Master) == 0u) Die("DMA PIO word in slave mode", addr, value);
    ctrl0_ = value | kCtrl0Run;
    dma_armed_ = true;
}

void Imx28I2c::CheckDmaCommand(bool transmit, uint32_t count) const {
    const bool xmit = (ctrl0_ & kCtrl0Transmit) != 0u;
    if (!dma_armed_ || xmit != transmit || count != (ctrl0_ & kCtrl0CountMask)) {
        Die("DMA data move with XFER_COUNT", base_, count);
    }
}

Imx28ApbDmaDevice::Transfer Imx28I2c::DmaFromMemory(uint32_t, const uint8_t* src, uint32_t count) {
    CheckDmaCommand(true, count);
    Transmit(src);
    return Transfer{count, false};
}

Imx28ApbDmaDevice::Transfer Imx28I2c::DmaToMemory(uint32_t, uint8_t* dst, uint32_t count) {
    CheckDmaCommand(false, count);
    master_.Receive(Decode(), dst);
    Complete(false);
    return Transfer{count, false};
}

bool Imx28I2c::DmaCommandEnded(uint32_t) {
    if (dma_armed_) Die("DMA command end with no data move", base_, ctrl0_);
    return true;
}

void Imx28I2c::DmaChannelReset(uint32_t) {
    if (!dma_armed_) return;
    dma_armed_ = false;
    ctrl0_ &= ~kCtrl0Run;
}

uint32_t Imx28I2c::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(a.reg_offset)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    if (a.reg_offset == kCtrl0) return ctrl0_;
    RequireOperating("ReadWord", ctrl0_, addr, 0);
    switch (a.reg_offset) {
        case kTiming0:   return timing0_;
        case kTiming1:   return timing1_;
        case kTiming2:   return timing2_;
        case kCtrl1:     return ctrl1_;
        case kStat:      return ComposeStat();
        case kQueueCtrl: return 0u;
        case kQueueStat: return kQueueStatReset;
        case kQueueCmd:  return 0u;
        case kQueueData: return 0u;
        case kData:      return ReadData(addr);
        case kDebug0:    return debug0_;
        case kDebug1:    return kDebug1Reset;
        case kVersion:   return kVersionValue;
        default:         break;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28I2c::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write && !HasSctAlias(a.reg_offset)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    if (a.reg_offset == kCtrl0) {
        WriteCtrl0(a.op, value, addr);
        return;
    }
    RequireOperating("WriteWord", ctrl0_, addr, value);
    switch (a.reg_offset) {
        case kTiming0: WriteTiming(timing0_, a.op, value, addr); return;
        case kTiming1: WriteTiming(timing1_, a.op, value, addr); return;
        case kTiming2: WriteTiming(timing2_, a.op, value, addr); return;
        case kCtrl1:   WriteCtrl1(a.op, value, addr);            return;
        case kData:    WriteData(value, addr);                    return;
        case kDebug0:  WriteDebug0(a.op, value, addr);            return;
        default:       break;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Imx28I2c::SaveState(StateWriter& w) {
    w.Write("ctrl0", ctrl0_);
    w.Write("timing0", timing0_);
    w.Write("timing1", timing1_);
    w.Write("timing2", timing2_);
    w.Write("ctrl1", ctrl1_);
    w.Write("debug0", debug0_);
    w.Write<uint8_t>("dma_armed", dma_armed_ ? 1u : 0u);
    w.Write("pio", pio_);
    w.Write("pio_pos", pio_pos_);
    w.Write<uint8_t>("tx_held", static_cast<uint8_t>((tx_held_ ? 1u : 0u) | (got_nak_ ? 2u : 0u)));
    w.Write("tx_word", tx_word_);
    w.Write("pio_len", static_cast<uint32_t>(pio_buf_.size()));
    w.WriteBytes("pio_buf", pio_buf_.data(), pio_buf_.size());
    master_.Save(w);
}

void Imx28I2c::RestoreState(StateReader& r) {
    uint8_t  armed = 0u;
    uint8_t  held  = 0u;
    uint32_t len   = 0u;
    r.Read("ctrl0", ctrl0_);
    r.Read("timing0", timing0_);
    r.Read("timing1", timing1_);
    r.Read("timing2", timing2_);
    r.Read("ctrl1", ctrl1_);
    r.Read("debug0", debug0_);
    r.Read("dma_armed", armed);
    dma_armed_ = armed != 0u;
    r.Read("pio", pio_);
    r.Read("pio_pos", pio_pos_);
    r.Read("tx_held", held);
    tx_held_ = (held & 1u) != 0u;
    got_nak_ = (held & 2u) != 0u;
    r.Read("tx_word", tx_word_);
    r.Read("pio_len", len);
    pio_buf_.assign(len, 0u);
    r.ReadBytes("pio_buf", pio_buf_.data(), pio_buf_.size());
    master_.Restore(r);
}

}
