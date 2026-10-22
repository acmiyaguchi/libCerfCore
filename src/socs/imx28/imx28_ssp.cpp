#include "imx28_ssp.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/mmc/mmc_card.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apbh.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_ssp_regs.h"

#include <cstring>
#include <vector>

namespace cerfcore {

using namespace imx28_ssp;

namespace {

/* Section 6.2: PIO words go to consecutive registers from the first one; sharp_brain_pw_sh4
   sdhc.dll SDHCStartDmaTransfer c07830d8 sends CTRL0, CMD0, CMD1, XFER_SIZE, BLOCK_SIZE. */
constexpr uint32_t kPioRegisters = 5u;
constexpr uint32_t kRegStride    = 0x10u;

/* Section 17.10: _SET, _CLR and _TOG exist for CTRL0, CMD0 and CTRL1. */
bool HasSctAlias(uint32_t reg) { return reg == kCtrl0 || reg == kCmd0 || reg == kCtrl1; }

}  /* namespace */

bool Imx28Ssp::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Ssp::OnReady() {
    icoll_ = &emu_.Get<Imx28Icoll>();
    emu_.Get<Imx28Apbh>().AttachDevice(unit_, this);
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Ssp::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 SSP%u: %s at %08X (value %08X, CTRL0 %08X CMD0 %08X CTRL1 %08X)",
                          unit_, what, addr, value, ctrl0_, cmd0_, ctrl1_);
}

/* Section 17.10 register figures, Reset rows. */
void Imx28Ssp::ResetBlock() {
    ctrl0_      = kCtrl0Reset;
    cmd0_       = 0u;
    cmd1_       = 0u;
    xfer_size_  = kXferSizeReset;
    block_size_ = 0u;
    compref_    = 0u;
    compmask_   = 0u;
    timing_     = 0u;
    ctrl1_      = kCtrl1Reset;
    ddr_ctrl_   = 0u;
    dll_ctrl_   = 0u;
    status_     = 0u;
    for (uint32_t& r : sdresp_) r = 0u;
    dma_pio_    = false;
    DriveLine(false);
}

/* Section 17.10.9: an IRQ status bit asserts the interrupt when its enable is set; Table 5-1
   sources 96, 97 ssp0/ssp1_error_irq. */
void Imx28Ssp::DriveLine(bool force) {
    const bool on = (ctrl1_ & (ctrl1_ << 1) & kCtrl1IrqStatus) != 0u;
    if (!force && on == line_) return;
    line_ = on;
    if (on) icoll_->AssertSource(error_irq_);
    else    icoll_->DeassertSource(error_irq_);
}

MmcCard* Imx28Ssp::Card() {
    auto* card = emu_.TryGet<MmcCard>();
    return (card != nullptr && card->SlotIndex() == unit_) ? card : nullptr;
}

/* Section 17.10.1 ENABLE, CHECK_RESP, WAIT_FOR_CMD, SDIO_IRQ_CHECK, LOCK_CS, BUS_WIDTH;
   section 17.10.2 CMD; section 17.10.9 SSP_MODE, WORD_LENGTH, SLAVE_MODE. */
void Imx28Ssp::CheckRun(uint32_t addr) const {
    RequireOperating("RUN", ctrl0_, addr, ctrl0_);
    if ((ctrl1_ & kCtrl1ModeMask) != kCtrl1ModeSdMmc) Die("RUN with SSP_MODE", addr, ctrl1_);
    if (((ctrl1_ >> kCtrl1WordLenShift) & kCtrl1WordLenMask) != kCtrl1WordLen8Bits) {
        Die("RUN with WORD_LENGTH", addr, ctrl1_);
    }
    if ((ctrl1_ & kCtrl1SlaveMode) != 0u) Die("RUN with SLAVE_MODE", addr, ctrl1_);
    if ((ctrl0_ & kCtrl0Enable) == 0u) Die("RUN with ENABLE clear", addr, ctrl0_);
    if ((ctrl0_ & (kCtrl0SdioIrqCheck | kCtrl0CheckResp | kCtrl0WaitForCmd | kCtrl0LockCs)) != 0u) {
        Die("RUN with SDIO_IRQ_CHECK, CHECK_RESP, WAIT_FOR_CMD or LOCK_CS", addr, ctrl0_);
    }
    if ((ctrl0_ & kCtrl0BusWidthMask) == kCtrl0BusWidthRsvd) Die("RUN with BUS_WIDTH 3", addr, ctrl0_);
    if ((cmd0_ & kCmd0Unmodelled) != 0u) {
        Die("RUN with SOFT_TERMINATE, DBL_DATA_RATE_EN, PRIM_BOOT_OP_EN or BOOT_ACK_EN", addr, cmd0_);
    }
    if ((cmd0_ & kCmd0CmdField & ~kCmd0CmdIndex) != 0u) Die("RUN with CMD bits [7:6]", addr, cmd0_);
}

/* Section 17.8.1: no response within 64 SCK cycles sets RESP_TIMEOUT; sections 17.10.11-14
   SDRESP0 [31:0] ... SDRESP3 [127:96]; section 17.10.17 status cleared on the rising edge of RUN. */
bool Imx28Ssp::IssueCommand(uint32_t addr) {
    CheckRun(addr);
    status_ &= ~kStatusRespTimeout;
    ctrl0_ |= kCtrl0Run;
    uint32_t resp[4] = {};
    MmcCard* card = Card();
    const MmcCommandResult result =
        card != nullptr ? card->Command(static_cast<uint8_t>(cmd0_ & kCmd0CmdIndex), cmd1_, resp)
                        : MmcCommandResult::NoResponse;
    if ((ctrl0_ & kCtrl0GetResp) == 0u) return true;
    if (result == MmcCommandResult::NoResponse) {
        ctrl1_  |= kCtrl1RespTimeoutIrq;
        status_ |= kStatusRespTimeout;
        return false;
    }
    const bool want_long = (ctrl0_ & kCtrl0LongResp) != 0u;
    if (want_long != (result == MmcCommandResult::Long)) {
        Die("card response length differs from LONG_RESP", addr, cmd1_);
    }
    if (want_long) {
        for (uint32_t i = 0; i < 4u; ++i) sdresp_[i] = resp[3u - i];
    } else {
        sdresp_[0] = resp[0];
    }
    return true;
}

/* sharp_brain_pw_sh4 sdhc.dll SDHCSendCommand c0784128 sets RUN for a command with no data and
   polls RUN clear. */
void Imx28Ssp::RunCommandOnly(uint32_t addr) {
    if ((ctrl0_ & kCtrl0DataXfer) != 0u) Die("RUN set by software with DATA_XFER", addr, ctrl0_);
    IssueCommand(addr);
    MmcCard* card = Card();
    if (card != nullptr && !card->ReadData().empty()) {
        Die("card staged read data for a command with no DATA_XFER", addr, cmd0_);
    }
    ctrl0_ &= ~kCtrl0Run;
    DriveLine(false);
}

/* Section 17.8.2 Figure 17-11: the DMA sets RUN after the PIO cycle; section 17.8.2.1 block size
   and count; section 17.10.9 DMA_ENABLE. */
uint32_t Imx28Ssp::BeginData(bool to_memory, uint32_t count) {
    const uint32_t addr = base_ + kCtrl0;
    if (!dma_pio_) Die("DMA data phase with no PIO words", addr, count);
    if ((ctrl1_ & kCtrl1DmaEnable) == 0u) Die("DMA data phase with DMA_ENABLE clear", addr, ctrl1_);
    if ((ctrl0_ & kCtrl0DataXfer) == 0u) Die("DMA data phase with DATA_XFER clear", addr, ctrl0_);
    if (((ctrl0_ & kCtrl0Read) != 0u) != to_memory) Die("DMA direction differs from READ", addr, ctrl0_);
    if (xfer_size_ != count) Die("DMA XFER_COUNT differs from XFER_SIZE", addr, count);
    const uint32_t blocks_less_one = (block_size_ >> kBlockCountShift) & kBlockCountMask;
    const uint32_t block =
        blocks_less_one != 0u ? (1u << (block_size_ & kBlockSizeFieldMask)) : xfer_size_;
    if (block == 0u || (blocks_less_one != 0u && uint64_t(block) * (blocks_less_one + 1u) != xfer_size_)) {
        Die("BLOCK_SIZE does not divide XFER_SIZE", addr, block_size_);
    }
    if (!IssueCommand(addr)) Die("data command response timeout", addr, cmd0_);
    if (Card() == nullptr) Die("data command with no card", addr, cmd0_);
    return block;
}

void Imx28Ssp::EndData() {
    ctrl0_ &= ~kCtrl0Run;
    dma_pio_ = false;
    DriveLine(false);
}

void Imx28Ssp::DmaPioWrite(uint32_t, uint32_t index, uint32_t value) {
    const uint32_t reg = index * kRegStride;
    if (index >= kPioRegisters) Die("DMA PIO word index", base_ + reg, value);
    if (index == 0u && (value & kCtrl0Run) != 0u) Die("DMA PIO CTRL0 word with RUN", base_, value);
    WriteRegister(reg, Imx28SctOp::Write, value, base_ + reg);
    dma_pio_ = true;
}

Imx28ApbDmaDevice::Transfer Imx28Ssp::DmaToMemory(uint32_t, uint8_t* dst, uint32_t count) {
    const uint32_t block = BeginData(true, count);
    MmcCard& card = *Card();
    for (uint32_t off = 0; off < count; off += block) {
        if (off != 0u) card.NextBlock();
        const std::vector<uint8_t>& data = card.ReadData();
        if (data.size() != block) Die("card block bytes differ from SSP block", base_, static_cast<uint32_t>(data.size()));
        std::memcpy(dst + off, data.data(), block);
    }
    card.EndDataPhase();
    EndData();
    return Transfer{count, false};
}

Imx28ApbDmaDevice::Transfer Imx28Ssp::DmaFromMemory(uint32_t, const uint8_t* src, uint32_t count) {
    const uint32_t block = BeginData(false, count);
    MmcCard& card = *Card();
    for (uint32_t off = 0; off < count; off += block) card.ReceiveBlock(src + off, block);
    EndData();
    return Transfer{count, false};
}

bool Imx28Ssp::DmaCommandEnded(uint32_t) {
    if (dma_pio_ || (ctrl0_ & kCtrl0Run) != 0u) Die("DMA command end with no data phase", base_, ctrl0_);
    return true;
}

uint32_t Imx28Ssp::CheckReserved(uint32_t next, uint32_t writable, uint32_t addr,
                                 uint32_t value) const {
    if ((next & ~writable) != 0u) Die("reserved bits", addr, value);
    return next;
}

void Imx28Ssp::WriteCtrl0(Imx28SctOp op, uint32_t value, uint32_t addr) {
    const uint32_t old  = ctrl0_;
    const uint32_t next = CheckReserved(Imx28ApplySct(op, old, value), kCtrl0Writable, addr, value);
    if (IsSoftReset(old, next)) {
        ResetBlock();
        return;
    }
    ctrl0_ = next;
    if ((old & kCtrl0Run) == 0u && (next & kCtrl0Run) != 0u) RunCommandOnly(addr);
}

void Imx28Ssp::WriteTiming(uint32_t value, uint32_t addr) {
    const uint32_t divide = (value >> kTimingDivideShift) & kTimingDivideMask;
    if (divide < 2u || (divide & 1u) != 0u) Die("TIMING CLOCK_DIVIDE", addr, value);
    timing_ = value;
}

void Imx28Ssp::WriteRegister(uint32_t reg, Imx28SctOp op, uint32_t value, uint32_t addr) {
    if (reg == kCtrl0) {
        WriteCtrl0(op, value, addr);
        return;
    }
    RequireOperating("WriteWord", ctrl0_, addr, value);
    switch (reg) {
        case kCmd0:
            cmd0_ = CheckReserved(Imx28ApplySct(op, cmd0_, value), kCmd0Writable, addr, value);
            return;
        case kCmd1:      cmd1_ = value;                                       return;
        case kXferSize:  xfer_size_ = value;                                  return;
        case kBlockSize: block_size_ = CheckReserved(value, kBlockSizeWritable, addr, value); return;
        case kCompref:   compref_ = value;                                    return;
        case kCompmask:  compmask_ = value;                                   return;
        case kTiming:    WriteTiming(value, addr);                            return;
        case kDdrCtrl:   ddr_ctrl_ = CheckReserved(value, kDdrCtrlWritable, addr, value); return;
        case kDllCtrl:   dll_ctrl_ = CheckReserved(value, kDllCtrlWritable, addr, value); return;
        case kData:      Die("DATA write", addr, value);
        case kCtrl1:
            ctrl1_ = CheckReserved(Imx28ApplySct(op, ctrl1_, value), kCtrl1Writable, addr, value);
            DriveLine(false);
            return;
        default:
            break;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

uint32_t Imx28Ssp::ReadWord(uint32_t addr) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (!HasSctAlias(a.reg_offset)) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    if (a.reg_offset == kCtrl0) return ctrl0_;
    /* Section 17.10.1 SFTRST, CLKGATE; sharp_brain_pw_sh4 nk.exe 80211ad4. */
    if (a.reg_offset == kStatus) return kStatusReset | status_;
    RequireOperating("ReadWord", ctrl0_, addr, 0);
    if (a.reg_offset >= kSdresp0 && a.reg_offset <= kSdresp3) {
        return sdresp_[(a.reg_offset - kSdresp0) / kRegStride];
    }
    switch (a.reg_offset) {
        case kCmd0:      return cmd0_;
        case kCmd1:      return cmd1_;
        case kXferSize:  return xfer_size_;
        case kBlockSize: return block_size_;
        case kCompref:   return compref_;
        case kCompmask:  return compmask_;
        case kTiming:    return timing_;
        case kCtrl1:     return ctrl1_;
        case kDdrCtrl:   return ddr_ctrl_;
        case kDllCtrl:   return dll_ctrl_;
        case kVersion:   return kVersionValue;
        case kData:      Die("DATA read", addr, 0u);
        case kDllSts:    Die("DLL_STS read", addr, 0u);
        case kDebug:     Die("DEBUG read", addr, 0u);
        default:         break;
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Ssp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - base_;
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write && !HasSctAlias(a.reg_offset)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    WriteRegister(a.reg_offset, a.op, value, addr);
}

void Imx28Ssp::SaveState(StateWriter& w) {
    w.Write("ctrl0", ctrl0_);
    w.Write("cmd0", cmd0_);
    w.Write("cmd1", cmd1_);
    w.Write("xfer_size", xfer_size_);
    w.Write("block_size", block_size_);
    w.Write("compref", compref_);
    w.Write("compmask", compmask_);
    w.Write("timing", timing_);
    w.Write("ctrl1", ctrl1_);
    w.Write("ddr_ctrl", ddr_ctrl_);
    w.Write("dll_ctrl", dll_ctrl_);
    w.Write("status", status_);
    w.WriteBytes("sdresp", sdresp_, sizeof(sdresp_));
    w.Write<uint8_t>("dma_pio", dma_pio_ ? 1u : 0u);
    MmcCard* card = Card();
    w.Write<uint8_t>("card", card != nullptr ? 1u : 0u);
    if (card != nullptr) card->SaveState(w);
}

void Imx28Ssp::RestoreState(StateReader& r) {
    uint8_t pio = 0u;
    r.Read("ctrl0", ctrl0_);
    r.Read("cmd0", cmd0_);
    r.Read("cmd1", cmd1_);
    r.Read("xfer_size", xfer_size_);
    r.Read("block_size", block_size_);
    r.Read("compref", compref_);
    r.Read("compmask", compmask_);
    r.Read("timing", timing_);
    r.Read("ctrl1", ctrl1_);
    r.Read("ddr_ctrl", ddr_ctrl_);
    r.Read("dll_ctrl", dll_ctrl_);
    r.Read("status", status_);
    r.ReadBytes("sdresp", sdresp_, sizeof(sdresp_));
    r.Read("dma_pio", pio);
    dma_pio_ = pio != 0u;
    uint8_t saved_card = 0u;
    r.Read("card", saved_card);
    MmcCard* card = Card();
    if ((saved_card != 0u) != (card != nullptr))
        emu_.Get<Fatal>().Die("Imx28 SSP%u: saved card presence %u differs", unit_, saved_card);
    if (card != nullptr) {
        card->RestoreState(r);
        card->PostRestore();
    }
}

}
