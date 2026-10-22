#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../i2c_slave.h"
#include "imx28_apb_dma_device.h"
#include "imx28_apb_peripheral.h"
#include "imx28_apbx.h"
#include "imx28_i2c_master.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_sct_reg.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

/* Section 27.5 memory map. */
constexpr uint32_t kCtrl0      = 0x000u;
constexpr uint32_t kTiming0    = 0x010u;
constexpr uint32_t kTiming1    = 0x020u;
constexpr uint32_t kTiming2    = 0x030u;
constexpr uint32_t kCtrl1      = 0x040u;
constexpr uint32_t kStat       = 0x050u;
constexpr uint32_t kQueueCtrl  = 0x060u;
constexpr uint32_t kQueueStat  = 0x070u;
constexpr uint32_t kQueueCmd   = 0x080u;
constexpr uint32_t kQueueData  = 0x090u;
constexpr uint32_t kData       = 0x0A0u;
constexpr uint32_t kDebug0     = 0x0B0u;
constexpr uint32_t kDebug1     = 0x0C0u;
constexpr uint32_t kVersion    = 0x0D0u;
constexpr uint32_t kMapEnd     = 0x0E0u;

/* Section 27.5 memory map, Reset value column. */
constexpr uint32_t kCtrl0Reset     = 0xC0000000u;
constexpr uint32_t kTiming0Reset   = 0x00780030u;
constexpr uint32_t kTiming1Reset   = 0x00800030u;
constexpr uint32_t kTiming2Reset   = 0x00300030u;
constexpr uint32_t kCtrl1Reset     = 0x08860000u;
constexpr uint32_t kQueueStatReset = 0x00002020u;
constexpr uint32_t kDebug0Reset    = 0x00100000u;
constexpr uint32_t kDebug1Reset    = 0xC0000000u;
constexpr uint32_t kVersionValue   = 0x01040000u;

/* Section 27.5.1 CTRL0. */
constexpr uint32_t kCtrl0Run         = 1u << 29;
constexpr uint32_t kCtrl0Rsvd2       = 1u << 28;
constexpr uint32_t kCtrl0PreAck      = 1u << 27;
constexpr uint32_t kCtrl0Acknowledge = 1u << 26;
constexpr uint32_t kCtrl0Rsvd1       = 1u << 24;
constexpr uint32_t kCtrl0MultiMaster = 1u << 23;
constexpr uint32_t kCtrl0ClockHeld   = 1u << 22;
constexpr uint32_t kCtrl0Retain      = 1u << 21;
constexpr uint32_t kCtrl0PostStop    = 1u << 20;
constexpr uint32_t kCtrl0PreStart    = 1u << 19;
constexpr uint32_t kCtrl0SlaveEnable = 1u << 18;
constexpr uint32_t kCtrl0Master      = 1u << 17;
constexpr uint32_t kCtrl0Transmit    = 1u << 16;
constexpr uint32_t kCtrl0CountMask   = 0xFFFFu;
constexpr uint32_t kCtrl0Unsupported = kCtrl0Rsvd2 | kCtrl0PreAck | kCtrl0Acknowledge |
                                       kCtrl0Rsvd1 | kCtrl0MultiMaster | kCtrl0SlaveEnable;

/* Sections 27.5.2-27.5.4: RSVD2 [31:26] and RSVD1 [15:10] are always zero. */
constexpr uint32_t kTimingMask = 0x03FF03FFu;

/* Section 27.5.5 CTRL1. */
constexpr uint32_t kCtrl1Rsvd1       = 1u << 31;
constexpr uint32_t kCtrl1QueueIrqs   = 3u << 29;
constexpr uint32_t kCtrl1ClrGotNak   = 1u << 28;
constexpr uint32_t kCtrl1NoSlaveAck  = 1u << 5;
constexpr uint32_t kCtrl1ForceIdle   = 3u << 25;
constexpr uint32_t kCtrl1BcastEn     = 1u << 24;
constexpr uint32_t kCtrl1Unsupported = kCtrl1Rsvd1 | kCtrl1QueueIrqs | kCtrl1ForceIdle |
                                       kCtrl1BcastEn;
constexpr uint32_t kCtrl1StatusMask  = 0xFFu;
constexpr uint32_t kCtrl1EnableShift = 8u;
constexpr uint32_t kCtrl1BusFreeIrq  = 1u << 7;
constexpr uint32_t kCtrl1DataEngineCmplt = 1u << 6;

/* Section 27.5.6 STAT. */
constexpr uint32_t kStatPresent     = 0xC0000000u;
constexpr uint32_t kStatAnyIrq      = 1u << 29;
constexpr uint32_t kStatDmaWait     = 1u << 12;
constexpr uint32_t kStatBusBusy     = 1u << 11;
constexpr uint32_t kStatEngineBusy  = 1u << 9;

/* Section 27.5.12 DEBUG0 DMAREQ [31]. */
constexpr uint32_t kDebug0DmaReq = 1u << 31;

/* Table 5-1 source 111 i2c0_error_irq; Table 7-1 channel 6 I2C0. */
constexpr uint32_t kErrorIrqSource = 111u;
constexpr uint32_t kDmaChannel     = 6u;

constexpr uint32_t kWordBytes = 4u;

bool HasSctAlias(uint32_t reg) {
    return reg == kCtrl0 || reg == kTiming0 || reg == kTiming1 || reg == kTiming2 ||
           reg == kCtrl1 || reg == kQueueCtrl || reg == kQueueCmd || reg == kDebug0 ||
           reg == kDebug1;
}

class Imx28I2c0 : public Imx28ApbPeripheral, private Imx28ApbDmaDevice {
public:
    explicit Imx28I2c0(CerfEmulator& emu) : Imx28ApbPeripheral(emu), master_(emu, kBase) {}

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx28;
    }

    void OnReady() override {
        icoll_ = &emu_.Get<Imx28Icoll>();
        master_.SetSlave(emu_.TryGet<I2cSlave>());
        emu_.Get<Imx28Apbx>().AttachDevice(kDmaChannel, this);
        ResetBlock();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;
    void     PostRestore() override { DriveLine(true); }

protected:
    uint32_t PeekLanes(uint32_t word_addr, uint32_t lanes) override {
        const uint32_t reg = (word_addr - kBase) & ~0xFu;
        return reg == kData || reg == kQueueData ? 0u : ReadLanes(word_addr, lanes);
    }

private:
    enum class Pio : uint8_t { Idle, Collect, Drain };

    static constexpr uint32_t kBase = 0x80058000u;

    void     ResetBlock();
    void     DriveLine(bool force);
    uint32_t ComposeStat() const;
    Imx28I2cMaster::Command Decode() const;
    void     CheckCtrl0(uint32_t value, uint32_t addr) const;
    void     WriteCtrl0(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteCtrl1(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteTiming(uint32_t& reg, Imx28SctOp op, uint32_t value, uint32_t addr);
    void     WriteDebug0(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     StartPio(uint32_t addr);
    void     WriteData(uint32_t value, uint32_t addr);
    uint32_t ReadData(uint32_t addr);
    void     Transmit(const uint8_t* data);
    void     Complete();
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    void     DmaPioWrite(uint32_t channel, uint32_t index, uint32_t value) override;
    Transfer DmaToMemory(uint32_t channel, uint8_t* dst, uint32_t count) override;
    Transfer DmaFromMemory(uint32_t channel, const uint8_t* src, uint32_t count) override;
    bool     DmaCommandEnded(uint32_t channel) override;
    void     DmaChannelReset(uint32_t channel) override;
    void     CheckDmaCommand(bool transmit, uint32_t count) const;

    Imx28Icoll*     icoll_ = nullptr;
    Imx28I2cMaster  master_;

    uint32_t ctrl0_   = 0u;
    uint32_t timing0_ = 0u;
    uint32_t timing1_ = 0u;
    uint32_t timing2_ = 0u;
    uint32_t ctrl1_   = 0u;
    uint32_t debug0_  = 0u;
    bool     dma_armed_ = false;
    bool     line_      = false;
    Pio      pio_       = Pio::Idle;
    uint32_t pio_pos_   = 0u;
    std::vector<uint8_t> pio_buf_;
};

}  /* namespace */

void Imx28I2c0::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 %08X: I2C %s at %08X (value %08X, CTRL0 %08X CTRL1 %08X)", kBase,
                          what, addr, value, ctrl0_, ctrl1_);
}

void Imx28I2c0::ResetBlock() {
    ctrl0_   = kCtrl0Reset;
    timing0_ = kTiming0Reset;
    timing1_ = kTiming1Reset;
    timing2_ = kTiming2Reset;
    ctrl1_   = kCtrl1Reset;
    debug0_  = kDebug0Reset;
    dma_armed_ = false;
    pio_       = Pio::Idle;
    pio_pos_   = 0u;
    pio_buf_.clear();
    master_.Reset();
    DriveLine(false);
}

/* Section 27.2.1: interrupt request is a CTRL1 status bit ANDed with its enable bit;
   Table 5-1 source 111. */
void Imx28I2c0::DriveLine(bool force) {
    const bool on = (ctrl1_ & (ctrl1_ >> kCtrl1EnableShift) & kCtrl1StatusMask) != 0u;
    if (!force && on == line_) return;
    line_ = on;
    if (on) icoll_->AssertSource(kErrorIrqSource);
    else    icoll_->DeassertSource(kErrorIrqSource);
}

/* Section 27.5.6 STAT: [7:0] are the CTRL1 status bits ANDed with their enables. */
uint32_t Imx28I2c0::ComposeStat() const {
    const uint32_t summary = ctrl1_ & (ctrl1_ >> kCtrl1EnableShift) & kCtrl1StatusMask;
    uint32_t stat = kStatPresent | summary;
    if (summary != 0u)                  stat |= kStatAnyIrq;
    if (master_.BusBusy())              stat |= kStatBusBusy;
    if (pio_ == Pio::Collect)           stat |= kStatDmaWait;
    if ((ctrl0_ & kCtrl0Run) != 0u)     stat |= kStatEngineBusy;
    return stat;
}

Imx28I2cMaster::Command Imx28I2c0::Decode() const {
    return Imx28I2cMaster::Command{(ctrl0_ & kCtrl0PreStart) != 0u,
                                   (ctrl0_ & kCtrl0PostStop) != 0u, ctrl0_ & kCtrl0CountMask};
}

/* Section 27.5.1 RSVD2, PRE_ACK, ACKNOWLEDGE, RSVD1, MULTI_MASTER, SLAVE_ADDRESS_ENABLE. */
void Imx28I2c0::CheckCtrl0(uint32_t value, uint32_t addr) const {
    if ((value & kCtrl0Unsupported) != 0u) Die("CTRL0 bits", addr, value);
}

void Imx28I2c0::WriteCtrl0(Imx28SctOp op, uint32_t value, uint32_t addr) {
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

void Imx28I2c0::WriteTiming(uint32_t& reg, Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, reg, value);
    if ((next & ~kTimingMask) != 0u) Die("timing register bits", addr, value);
    reg = next;
}

void Imx28I2c0::WriteCtrl1(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, ctrl1_, value & ~kCtrl1ClrGotNak);
    if ((next & kCtrl1Unsupported) != 0u) Die("CTRL1 bits", addr, value);
    ctrl1_ = next;
    DriveLine(false);
}

void Imx28I2c0::WriteDebug0(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t next = Imx28ApplySct(op, debug0_, value);
    if (((next ^ debug0_) & ~kDebug0DmaReq) != 0u) Die("DEBUG0 bits", addr, value);
    debug0_ = next;
}

/* Section 27.3.1 steps 1-3. */
void Imx28I2c0::StartPio(uint32_t addr) {
    RequireOperating("RUN", ctrl0_, addr, 0u);
    if ((ctrl0_ & kCtrl0Master) == 0u) Die("RUN in slave mode", addr, 0u);
    const Imx28I2cMaster::Command cmd = Decode();
    pio_buf_.assign(cmd.count, 0u);
    pio_pos_ = 0u;
    if ((ctrl0_ & kCtrl0Transmit) != 0u) {
        pio_ = Pio::Collect;
    } else {
        master_.Receive(cmd, pio_buf_.data());
        pio_ = Pio::Drain;
    }
    debug0_ |= kDebug0DmaReq;
}

/* Section 27.3.1 step 4; Figure 27-9 (word 0x03020156 sends 0x56 first). */
void Imx28I2c0::WriteData(uint32_t value, uint32_t addr) {
    if (pio_ != Pio::Collect) Die("DATA write with no transmit pending", addr, value);
    const uint32_t left = static_cast<uint32_t>(pio_buf_.size()) - pio_pos_;
    const uint32_t n    = std::min(kWordBytes, left);
    for (uint32_t k = 0; k < n; ++k) {
        pio_buf_[pio_pos_ + k] = static_cast<uint8_t>(value >> (8u * k));
    }
    pio_pos_ += n;
    if (pio_pos_ < pio_buf_.size()) return;
    Transmit(pio_buf_.data());
}

/* Table 27-1 No Slave Ack sets NO_SLAVE_ACK_IRQ; the RM leaves RUN and DATA_ENGINE_CMPLT_IRQ
   after the NAK undefined. */
void Imx28I2c0::Transmit(const uint8_t* data) {
    if (!master_.Transmit(Decode(), data)) {
        ctrl1_ |= kCtrl1NoSlaveAck;
        Die("no slave acknowledges address byte", kBase, data[0]);
    }
    Complete();
}

/* Section 27.3.1 steps 17-20 (word layout not defined); sharp_brain_pw_sh4 nk.exe
   OAL_I2CProcessPackets 802160f0 unpacks most significant byte first, right-justified. */
uint32_t Imx28I2c0::ReadData(uint32_t addr) {
    if (pio_ != Pio::Drain) Die("DATA read with no receive pending", addr, 0u);
    const uint32_t left = static_cast<uint32_t>(pio_buf_.size()) - pio_pos_;
    const uint32_t n    = std::min(kWordBytes, left);
    uint32_t word = 0u;
    for (uint32_t k = 0; k < n; ++k) word = (word << 8) | pio_buf_[pio_pos_ + k];
    pio_pos_ += n;
    if (pio_pos_ >= pio_buf_.size()) Complete();
    return word;
}

/* Section 27.5.1 PRE_SEND_START, POST_SEND_STOP cleared by hardware, RUN reset at the end of the
   command, CLOCK_HELD set after RETAIN_CLOCK; section 27.5.5 DATA_ENGINE_CMPLT_IRQ; Table 27-1
   BUS_FREE_IRQ on stop. */
void Imx28I2c0::Complete() {
    const bool retain = (ctrl0_ & kCtrl0Retain) != 0u;
    const bool stop   = (ctrl0_ & kCtrl0PostStop) != 0u;
    ctrl0_ &= ~(kCtrl0Run | kCtrl0PreStart | kCtrl0PostStop | kCtrl0ClockHeld | kCtrl0CountMask);
    if (retain) ctrl0_ |= kCtrl0ClockHeld;
    ctrl1_ |= kCtrl1DataEngineCmplt;
    if (stop) ctrl1_ |= kCtrl1BusFreeIrq;
    dma_armed_ = false;
    pio_       = Pio::Idle;
    pio_pos_   = 0u;
    pio_buf_.clear();
    DriveLine(false);
}

/* Section 27.2.3.1 Figure 27-9: the DMA PIO word is the CTRL0 value of the command. */
void Imx28I2c0::DmaPioWrite(uint32_t, uint32_t index, uint32_t value) {
    const uint32_t addr = kBase + index * kWordBytes;
    if (index != 0u) Die("DMA PIO word index", addr, value);
    RequireOperating("DmaPioWrite", ctrl0_, addr, value);
    if (pio_ != Pio::Idle || dma_armed_) Die("DMA PIO word with a command running", addr, value);
    if (Gated(value)) Die("DMA PIO word with SFTRST or CLKGATE", addr, value);
    CheckCtrl0(value, addr);
    if ((value & kCtrl0Master) == 0u) Die("DMA PIO word in slave mode", addr, value);
    ctrl0_ = value | kCtrl0Run;
    dma_armed_ = true;
}

void Imx28I2c0::CheckDmaCommand(bool transmit, uint32_t count) const {
    const bool xmit = (ctrl0_ & kCtrl0Transmit) != 0u;
    if (!dma_armed_ || xmit != transmit || count != (ctrl0_ & kCtrl0CountMask)) {
        Die("DMA data move with XFER_COUNT", kBase, count);
    }
}

Imx28ApbDmaDevice::Transfer Imx28I2c0::DmaFromMemory(uint32_t, const uint8_t* src, uint32_t count) {
    CheckDmaCommand(true, count);
    Transmit(src);
    return Transfer{count, false};
}

Imx28ApbDmaDevice::Transfer Imx28I2c0::DmaToMemory(uint32_t, uint8_t* dst, uint32_t count) {
    CheckDmaCommand(false, count);
    master_.Receive(Decode(), dst);
    Complete();
    return Transfer{count, false};
}

bool Imx28I2c0::DmaCommandEnded(uint32_t) {
    if (dma_armed_) Die("DMA command end with no data move", kBase, ctrl0_);
    return true;
}

void Imx28I2c0::DmaChannelReset(uint32_t) {
    if (!dma_armed_) return;
    dma_armed_ = false;
    ctrl0_ &= ~kCtrl0Run;
}

uint32_t Imx28I2c0::ReadWord(uint32_t addr) {
    const uint32_t off = addr - kBase;
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

void Imx28I2c0::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - kBase;
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

void Imx28I2c0::SaveState(StateWriter& w) {
    w.Write("ctrl0", ctrl0_);
    w.Write("timing0", timing0_);
    w.Write("timing1", timing1_);
    w.Write("timing2", timing2_);
    w.Write("ctrl1", ctrl1_);
    w.Write("debug0", debug0_);
    w.Write<uint8_t>("dma_armed", dma_armed_ ? 1u : 0u);
    w.Write("pio", pio_);
    w.Write("pio_pos", pio_pos_);
    w.Write("pio_len", static_cast<uint32_t>(pio_buf_.size()));
    w.WriteBytes("pio_buf", pio_buf_.data(), pio_buf_.size());
    master_.Save(w);
}

void Imx28I2c0::RestoreState(StateReader& r) {
    uint8_t  armed = 0u;
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
    r.Read("pio_len", len);
    pio_buf_.assign(len, 0u);
    r.ReadBytes("pio_buf", pio_buf_.data(), pio_buf_.size());
    master_.Restore(r);
}

REGISTER_SERVICE(Imx28I2c0);

}
