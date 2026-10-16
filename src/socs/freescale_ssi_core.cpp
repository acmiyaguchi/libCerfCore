#include "freescale_ssi_core.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "freescale_ssi_external_clock.h"
#include "freescale_ssi_regs.h"

#include <utility>

namespace cerfcore {

namespace ssi = cerf_freescale_ssi;

namespace {

constexpr uint32_t kOffStx0    = 0x00u;
constexpr uint32_t kOffStx1    = 0x04u;
constexpr uint32_t kOffSrx0    = 0x08u;
constexpr uint32_t kOffSrx1    = 0x0Cu;
constexpr uint32_t kOffScr     = 0x10u;
constexpr uint32_t kOffSisr    = 0x14u;
constexpr uint32_t kOffSier    = 0x18u;
constexpr uint32_t kOffStcr    = 0x1Cu;
constexpr uint32_t kOffSrcr    = 0x20u;
constexpr uint32_t kOffStccr   = 0x24u;
constexpr uint32_t kOffSrccr   = 0x28u;
constexpr uint32_t kOffSfcsr   = 0x2Cu;
constexpr uint32_t kOffStr     = 0x30u;
constexpr uint32_t kOffSacnt   = 0x38u;
constexpr uint32_t kOffSacadd  = 0x3Cu;
constexpr uint32_t kOffSacdat  = 0x40u;
constexpr uint32_t kOffSatag   = 0x44u;
constexpr uint32_t kOffStmsk   = 0x48u;
constexpr uint32_t kOffSrmsk   = 0x4Cu;
constexpr uint32_t kOffSaccst  = 0x50u;
constexpr uint32_t kOffSaccen  = 0x54u;
constexpr uint32_t kOffSaccdis = 0x58u;

/* MCIMX51RM Table 56-7 (pages 56-25 / 56-26) and MCIMX31RM Table 45-4 (pages 45-20 / 45-21):
   reset values. */
constexpr uint32_t kSierReset  = 0x00003003u;
constexpr uint32_t kCrReset    = 0x00000200u;
constexpr uint32_t kCcrReset   = 0x00040000u;
constexpr uint32_t kSfcsrReset = 0x00810081u;
constexpr uint32_t kStrReset   = 0x00001111u;

constexpr uint32_t kSfcsrWatermarks = 0x00FF00FFu;
constexpr uint32_t kSacntBits       = 0x000007FFu;
constexpr uint32_t kSacaddBits      = 0x0007FFFFu;
constexpr uint32_t kSacdatBits      = 0x000FFFFFu;
constexpr uint32_t kSatagBits       = 0x0000FFFFu;
constexpr uint32_t kChannelBits     = 0x000003FFu;

/* MCIMX51RM page 56-41 and MCIMX31RM page 45-35: "All the flags in the SISR are updated after the
   first bit of the next SSI word has completed transmission or reception." */
constexpr uint32_t kFlagDelayBits = 1u;

/* MCIMX51RM §56.1.2.5 (pages 56-16 / 56-17): "a 16-bit Tag Slot at the start of a frame and the
   rest of the slots (in that frame) are all 20-bits wide". */
constexpr uint32_t kAc97TagBits  = 16u;
constexpr uint32_t kAc97SlotBits = 20u;

}

FreescaleSsiCore::FreescaleSsiCore(CerfEmulator& emu, uint32_t ssi,
                                   const FreescaleSsiCoreConfig& config)
    : emu_(emu), ssi_(ssi), cfg_(config),
      tx_(emu, config.depth, config.setup_clocks, kFlagDelayBits),
      rx_(emu, config.depth, config.setup_clocks, kFlagDelayBits),
      status_(emu, tx_, rx_, config.status),
      tx_port_{StatusPort(tx_.DmaPort(0u), status_), StatusPort(tx_.DmaPort(1u), status_)},
      rx_port_{StatusPort(rx_.DmaPort(0u), status_), StatusPort(rx_.DmaPort(1u), status_)} {}

void FreescaleSsiCore::Attach(std::function<GuestCycleClock::Rate()> ssi_clock,
                              std::function<bool()> clocked, std::function<void(bool)> line) {
    ssi_clock_ = std::move(ssi_clock);
    clocked_   = std::move(clocked);
    tx_.Attach();
    rx_.Attach();
    status_.Attach(std::move(line));
    Reset();
}

void FreescaleSsiCore::Reset() {
    scr_   = 0u;
    sier_  = kSierReset;
    stcr_  = srcr_ = kCrReset;
    stccr_ = srccr_ = kCcrReset;
    sfcsr_ = kSfcsrReset;
    str_   = kStrReset;
    sacnt_ = sacadd_ = sacdat_ = tx_tag_ = stmsk_ = srmsk_ = saccst_ = cmd_bits_ = 0u;
    tx_.Reset();
    rx_.Reset();
    status_.Reset();
    Apply(false);
}

void FreescaleSsiCore::RegisterControlListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void FreescaleSsiCore::Notify() {
    for (auto& fn : listeners_) fn();
}

bool FreescaleSsiCore::Enabled() const { return (scr_ & ssi::kScrSsien) != 0u; }
bool FreescaleSsiCore::Ac97() const { return (sacnt_ & ssi::kSacntAc97en) != 0u; }

/* MCIMX51RM §56.1.2.4 (pages 56-15 / 56-16): I2S master "Network mode is selected (SCR[3]=1)", I2S
   slave "Normal mode is selected (SCR[3]=0)"; §56.1.2.5: in AC97 mode "Sync mode is entered" and
   "Network mode is selected". */
bool FreescaleSsiCore::Network() const {
    const uint32_t i2s = (scr_ >> ssi::kScrI2sShift) & 0x3u;
    if (Ac97() || i2s == ssi::kI2sMaster) return true;
    if (i2s == ssi::kI2sSlave) return false;
    return (scr_ & ssi::kScrNet) != 0u;
}

bool FreescaleSsiCore::Sync() const { return Ac97() || (scr_ & ssi::kScrSyn) != 0u; }

/* MCIMX31RM §45.1.2.5.2 (page 45-18): "the SSI transmits at the start of a frame only if there was a
   request for data in the previous frame". */
bool FreescaleSsiCore::TxRun() const {
    const bool te = (scr_ & ssi::kScrTe) != 0u;
    if (!Ac97() || (sacnt_ & ssi::kSacntFv) == 0u || cfg_.variable_frames) return te;
    return false;
}

/* MCIMX51RM §56.1.2.5 (page 56-17): "Tx FIFO is enabled (STCR[7]=1)", "Rx FIFO is enabled
   (SRCR[7]=1)", "TFDIR bit (STCR[6]) is forced to 1", "TXDIR bit (STCR[5]) is forced to 0". */
uint32_t FreescaleSsiCore::Stcr() const {
    if (!Ac97()) return stcr_;
    return (stcr_ | ssi::kCrFdir | ssi::kCrFen0) & ~ssi::kCrDir;
}

uint32_t FreescaleSsiCore::Srcr() const {
    return Ac97() ? (srcr_ | ssi::kCrFen0) : srcr_;
}

/* MCIMX51RM Table 56-5 (page 56-25): TXDIR selects the bit clock and TFDIR the frame sync, each in
   or out on its own. */
FreescaleSsiFrameShape FreescaleSsiCore::ClockShape(uint32_t cr, uint32_t ccr) {
    const bool int_bits  = (cr & ssi::kCrDir) != 0u;
    const bool int_frame = (cr & ssi::kCrFdir) != 0u;
    FreescaleSsiFrameShape board;
    if (!int_bits || !int_frame) board = emu_.Get<FreescaleSsiExternalClock>().Shape(ssi_);
    const FreescaleSsiFrameShape internal = FreescaleSsiSlotGrid::InternalClockShape(
        scr_, ccr, int_bits ? ssi_clock_() : GuestCycleClock::Rate{0u, 1u});
    FreescaleSsiFrameShape shape;
    shape.bit_rate = int_bits ? internal.bit_rate : board.bit_rate;
    if (Ac97()) {
        shape.slots    = internal.slots;
        shape.bits     = kAc97SlotBits;
        shape.tag_bits = kAc97TagBits;
        shape.period   = ((sacnt_ >> ssi::kSacntFrdivShift) & ssi::kSacntFrdivMask) + 1u;
    } else if (int_frame) {
        shape.slots = internal.slots;
        shape.bits  = internal.bits;
    } else {
        shape.slots = board.slots;
        shape.bits  = board.bits;
    }
    return shape;
}

FreescaleSsiFrameShape FreescaleSsiCore::SectionShape(uint32_t cr, uint32_t ccr) {
    if (!Enabled() || !clocked_()) return FreescaleSsiFrameShape{};
    return ClockShape(cr, ccr);
}

FreescaleSsiFrameShape FreescaleSsiCore::TxShape() { return SectionShape(Stcr(), stccr_); }

/* MCIMX51RM §56.1.2.2 (page 56-9): in Two-Channel mode "data is transmitted/received in enabled
   time slots alternately from/to FIFO 0 and FIFO 1, starting from FIFO 0". */
GuestCycleClock::Rate FreescaleSsiCore::TxFifo0WordRate() {
    const FreescaleSsiFrameShape shape = ClockShape(Stcr(), stccr_);
    if (!shape.Known()) return GuestCycleClock::Rate{0u, 1u};
    uint64_t words = 0u;
    for (uint32_t data = TxData(shape); data != 0u; data &= data - 1u) ++words;
    const uint64_t channels = (scr_ & ssi::kScrTchEn) != 0u ? 2u : 1u;
    return GuestCycleClock::Rate{shape.bit_rate.num * words,
                                 shape.bit_rate.den * shape.FrameBits() * shape.period * channels};
}

/* MCIMX31RM Table 45-9 SYN (page 45-33): "In synchronous mode, the transmit and receive sections of
   SSI share a common clock port (STCK) and frame sync port (STFS)." */
FreescaleSsiFrameShape FreescaleSsiCore::RxShape() {
    return Sync() ? TxShape() : SectionShape(Srcr(), srccr_);
}

/* MCIMX51RM §56.1.2.5.1 (page 56-18): "The data from TX-FIFO is transmitted in Slot #3-Slot #12
   depending on the valid slots indicated by the TAG value"; AC'97 2.2 Table 9 (page 31): slot n
   valid is tag bit 15 - n. MCIMX31RM §45.1.2.5.1 (page 45-18): "(slots #3, #4)". */
uint32_t FreescaleSsiCore::TxData(const FreescaleSsiFrameShape& shape) const {
    if (!Ac97()) return FreescaleSsiSlotGrid::DataSlots(Network(), shape.slots, stmsk_);
    if ((sacnt_ & ssi::kSacntFv) != 0u) return 0u;
    uint32_t data = 0u;
    for (uint32_t n = 3u; n <= 12u && n < shape.slots; ++n) {
        const bool valid = cfg_.tag_slots ? ((tx_tag_ >> (15u - n)) & 1u) != 0u : n <= 4u;
        if (valid) data |= 1u << n;
    }
    return data;
}

/* MCIMX51RM §56.1.2.5.1 (page 56-18): "bit 15 of the TAG Value (Slot #0) is checked to see if the
   CODEC is ready. If this bit is set, the frame is received." */
uint32_t FreescaleSsiCore::RxData(const FreescaleSsiFrameShape& shape) const {
    if (Ac97()) return 0u;
    return FreescaleSsiSlotGrid::DataSlots(Network(), shape.slots, srmsk_);
}

FreescaleSsiSectionControl FreescaleSsiCore::Section(bool tx,
                                                     const FreescaleSsiFrameShape& shape) const {
    FreescaleSsiSectionControl c;
    const uint32_t cr = tx ? Stcr() : Srcr();
    c.enabled     = Enabled();
    c.run         = tx ? TxRun() : (scr_ & ssi::kScrRe) != 0u;
    c.two_channel = (scr_ & ssi::kScrTchEn) != 0u;
    c.dma         = clocked_() && (sier_ & (tx ? ssi::kSierTdmae : ssi::kSierRdmae)) != 0u;
    for (uint32_t k = 0; k < 2u; ++k) {
        c.fen[k] = (cr & ssi::CrFen(k)) != 0u;
        c.wm[k]  = ssi::SfcsrField(sfcsr_, tx ? ssi::kSfcsrTfwm : ssi::kSfcsrRfwm, k);
    }
    c.data    = tx ? TxData(shape) : RxData(shape);
    c.network = Network();
    return c;
}

FreescaleSsiStatusControl FreescaleSsiCore::StatusControl() const {
    FreescaleSsiStatusControl c;
    c.enabled     = Enabled();
    c.te          = (scr_ & ssi::kScrTe) != 0u;
    c.re          = (scr_ & ssi::kScrRe) != 0u;
    c.network     = Network();
    c.two_channel = (scr_ & ssi::kScrTchEn) != 0u;
    for (uint32_t k = 0; k < 2u; ++k) {
        c.tfen[k] = (Stcr() & ssi::CrFen(k)) != 0u;
        c.rfen[k] = (Srcr() & ssi::CrFen(k)) != 0u;
    }
    c.sier = sier_;
    return c;
}

void FreescaleSsiCore::Apply(bool notify) {
    const FreescaleSsiFrameShape tx_shape = TxShape();
    const FreescaleSsiFrameShape rx_shape = RxShape();
    tx_.Configure(Section(true, tx_shape), tx_shape, Ac97());
    rx_.Configure(Section(false, rx_shape), rx_shape);
    status_.SetControl(StatusControl());
    if (notify) Notify();
}

uint32_t FreescaleSsiCore::Sfcsr() {
    uint32_t v = sfcsr_ & kSfcsrWatermarks;
    for (uint32_t k = 0; k < 2u; ++k) {
        v |= tx_.Level(k) << (ssi::kSfcsrTfcnt + 16u * k);
        v |= rx_.Level(k) << (ssi::kSfcsrRfcnt + 16u * k);
    }
    return v;
}

/* MCIMX51RM Table 56-22 (page 56-61): WR / RD "automatically cleared by the SSI after completing
   transmission of a frame". */
void FreescaleSsiCore::WriteSacnt(uint32_t value) {
    sacnt_    = value & kSacntBits;
    cmd_bits_ = value & (ssi::kSacntWr | ssi::kSacntRd);
    Apply(false);
    tx_.SetCommand(cmd_bits_ != 0u);
    Notify();
}

bool FreescaleSsiCore::Read(uint32_t off, uint32_t& value) {
    value = 0u;
    switch (off) {
        case kOffSrx0:
        case kOffSrx1: {
            if (!clocked_()) return true;
            const uint32_t k = off == kOffSrx0 ? 0u : 1u;
            rx_.ReadSrx(k);
            status_.SrxRead(k);
            Notify();
            return true;
        }
        case kOffScr:    value = scr_; return true;
        case kOffSisr:   value = status_.ReadSisr(); return true;
        case kOffSier:   value = sier_; return true;
        case kOffStcr:   value = stcr_; return true;
        case kOffSrcr:   value = srcr_; return true;
        case kOffStccr:  value = stccr_; return true;
        case kOffSrccr:  value = srccr_; return true;
        case kOffSfcsr:  value = Sfcsr(); return true;
        case kOffSacnt:
            value = (sacnt_ & ~(ssi::kSacntWr | ssi::kSacntRd)) |
                    (tx_.CommandPending() ? cmd_bits_ : 0u);
            return true;
        case kOffSacadd: value = sacadd_; return true;
        case kOffSacdat: value = sacdat_; return true;
        /* MCIMX51RM Table 56-25 (page 56-64): "On a read, the Core gets the Rx-Tag Value received
           (in the last frame) from the Codec." */
        case kOffSatag:  return true;
        case kOffStmsk:  value = stmsk_; return true;
        case kOffSrmsk:  value = srmsk_; return true;
        case kOffStr:
            if (!cfg_.test_register) return false;
            value = str_;
            return true;
        case kOffSaccst:
            if (!cfg_.channel_requests) return false;
            value = saccst_;
            return true;
        /* MCIMX51RM Table 56-29 (page 56-68): "These bits are always read as '0' by the Core." */
        case kOffSaccen:
        case kOffSaccdis: return cfg_.channel_requests;
    }
    return false;
}

bool FreescaleSsiCore::Write(uint32_t off, uint32_t value) {
    switch (off) {
        case kOffStx0:
        case kOffStx1: {
            if (!clocked_()) return true;
            const uint32_t k = off == kOffStx0 ? 0u : 1u;
            tx_.WriteStx(k);
            status_.StxWrite(k);
            Notify();
            return true;
        }
        case kOffScr:    scr_ = value; Apply(true); return true;
        case kOffSisr:   status_.WriteSisr(value); return true;
        case kOffSier:   sier_ = value; Apply(true); return true;
        case kOffStcr:   stcr_ = value; Apply(true); return true;
        case kOffSrcr:   srcr_ = value; Apply(true); return true;
        case kOffStccr:  stccr_ = value; Apply(true); return true;
        case kOffSrccr:  srccr_ = value; Apply(true); return true;
        case kOffSfcsr:  sfcsr_ = value; Apply(true); return true;
        case kOffSacnt:  WriteSacnt(value); return true;
        case kOffSacadd: sacadd_ = value & kSacaddBits; return true;
        case kOffSacdat: sacdat_ = value & kSacdatBits; return true;
        /* MCIMX51RM Table 56-25 (page 56-64): "Writing to this register (by the Core) sets the
           value of the Tx-Tag in AC97 fixed mode of operation." */
        case kOffSatag:  tx_tag_ = value & kSatagBits; Apply(true); return true;
        case kOffStmsk:  stmsk_ = value; Apply(true); return true;
        case kOffSrmsk:  srmsk_ = value; Apply(true); return true;
        case kOffStr:
            if (!cfg_.test_register) return false;
            str_ = value;
            return true;
        /* MCIMX51RM Table 56-30 (page 56-69): "Writes to these bits only have effect in the AC97
           Variable mode of operation." */
        case kOffSaccen:
        case kOffSaccdis:
            if (!cfg_.channel_requests) return false;
            if (Ac97() && (sacnt_ & ssi::kSacntFv) != 0u) {
                if (off == kOffSaccen) saccst_ |= value & kChannelBits;
                else                   saccst_ &= ~(value & kChannelBits);
            }
            return true;
    }
    return false;
}

void FreescaleSsiCore::OnCpuRate() {
    tx_.OnCpuRate();
    rx_.OnCpuRate();
    status_.Update();
    Notify();
}

void FreescaleSsiCore::OnClockChange() {
    if (!Enabled()) return;
    Apply(true);
}

void FreescaleSsiCore::Save(StateWriter& w) {
    w.Write("scr", scr_);
    w.Write("sier", sier_);
    w.Write("stcr", stcr_);
    w.Write("srcr", srcr_);
    w.Write("stccr", stccr_);
    w.Write("srccr", srccr_);
    w.Write("sfcsr", sfcsr_);
    w.Write("str", str_);
    w.Write("sacnt", sacnt_);
    w.Write("sacadd", sacadd_);
    w.Write("sacdat", sacdat_);
    w.Write("tx_tag", tx_tag_);
    w.Write("stmsk", stmsk_);
    w.Write("srmsk", srmsk_);
    w.Write("saccst", saccst_);
    w.Write("cmd_bits", cmd_bits_);
    tx_.Save(w);
    rx_.Save(w);
    status_.Save(w);
}

void FreescaleSsiCore::Restore(StateReader& r) {
    r.Read("scr", scr_);
    r.Read("sier", sier_);
    r.Read("stcr", stcr_);
    r.Read("srcr", srcr_);
    r.Read("stccr", stccr_);
    r.Read("srccr", srccr_);
    r.Read("sfcsr", sfcsr_);
    r.Read("str", str_);
    r.Read("sacnt", sacnt_);
    r.Read("sacadd", sacadd_);
    r.Read("sacdat", sacdat_);
    r.Read("tx_tag", tx_tag_);
    r.Read("stmsk", stmsk_);
    r.Read("srmsk", srmsk_);
    r.Read("saccst", saccst_);
    r.Read("cmd_bits", cmd_bits_);
    tx_.Restore(r);
    rx_.Restore(r);
    status_.Restore(r);
}

void FreescaleSsiCore::PostRestore() { status_.PostRestore(StatusControl()); }

}
