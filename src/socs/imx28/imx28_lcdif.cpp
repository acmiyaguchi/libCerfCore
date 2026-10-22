#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../mpu_lcd_panel.h"
#include "imx28_apb_peripheral.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_lcdif_regs.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

using namespace imx28_lcdif;

/* MCIMX28RM Rev. 2 Table 5-1: source 38 lcdif_irq. */
constexpr uint32_t kIrqSource = 38u;

/* Section 33.4.1 CTRL fields the model drives at RUN. */
constexpr uint32_t kCtrlUnmodelled = kCtrlDataFormat16 | kCtrlRgbToYcbcr | kCtrlWordLength |
                                     kCtrlDatabusWidth | kCtrlCscSwizzle | kCtrlInputSwizzle |
                                     kCtrlDotclkMode | kCtrlBypassCount | kCtrlDviMode |
                                     kCtrlShiftNumBits | kCtrlReadWriteb | kCtrlYcbcr422Input;

/* MCIMX28RM Rev. 2 chapter 33 (LCDIF) at 0x8003_0000. */
class Imx28Lcdif : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx28;
    }

    void OnReady() override {
        icoll_ = &emu_.Get<Imx28Icoll>();
        panel_ = emu_.TryGet<MpuLcdPanel>();
        ResetBlock(false);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetBlock(false);
            DriveIrq();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x80030000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override {
        w.Write("regs", regs_);
        w.Write("pio_left", pio_left_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("regs", regs_);
        r.Read("pio_left", pio_left_);
    }

    void PostRestore() override { DriveIrq(); }

private:
    static int SpecIndex(uint32_t reg);

    uint32_t& Reg(uint32_t reg) { return regs_[SpecIndex(reg)]; }

    void ResetBlock(bool keep_lcd_reset);
    void WriteStorage(int index, Imx28SctOp op, uint32_t addr, uint32_t value);
    void WriteCtrl(Imx28SctOp op, uint32_t addr, uint32_t value);
    void WriteCtrl1(Imx28SctOp op, uint32_t addr, uint32_t value);
    void Start(uint32_t addr);
    void RunFrameDma();
    void WriteData(uint32_t addr, uint32_t value);
    void SendWord(uint16_t word);
    void FinishTransfer();
    void DriveIrq();
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    Imx28Icoll*  icoll_ = nullptr;
    MpuLcdPanel* panel_ = nullptr;

    uint32_t regs_[kStorageCount] = {};
    uint32_t pio_left_ = 0u;
};

int Imx28Lcdif::SpecIndex(uint32_t reg) {
    for (uint32_t i = 0; i < kStorageCount; ++i)
        if (kStorage[i].offset == reg) return static_cast<int>(i);
    return -1;
}

void Imx28Lcdif::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 LCDIF: %s at %08X (value %08X, CTRL %08X, CTRL1 %08X)", what,
                          addr, value, regs_[0], regs_[1]);
}

/* Sections 33.3, 33.4.2 CTRL1 RESET is not reset by SFTRST. */
void Imx28Lcdif::ResetBlock(bool keep_lcd_reset) {
    const uint32_t lcd_reset = Reg(kCtrl1) & kCtrl1Reset;
    for (uint32_t i = 0; i < kStorageCount; ++i) regs_[i] = kStorage[i].reset;
    if (keep_lcd_reset) Reg(kCtrl1) |= lcd_reset;
    pio_left_ = 0u;
}

uint32_t Imx28Lcdif::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) HaltUnsupportedAccess("ReadWord", addr, 0);
    if (a.reg_offset == kStat) return kStatIdle;
    if (a.reg_offset == kVersion) return kVersionValue;
    const int index = SpecIndex(a.reg_offset);
    if (index < 0) Die("read of unmodelled register", addr, 0u);
    return regs_[index];
}

void Imx28Lcdif::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl) {
        WriteCtrl(a.op, addr, value);
        return;
    }
    /* Section 33.4.1 CTRL SFTRST: set to one, it forces a block level reset; sharp_brain_pw_sh4
       ddraw_mx28.dll PanelPowerOn c06d74a0 writes CTRL1 before LcdifResetSeq. */
    if ((Reg(kCtrl) & kCtrlSftrst) != 0u) return;
    RequireOperating("register write", Reg(kCtrl), addr, value);
    if (a.reg_offset == kData) {
        if (a.op != Imx28SctOp::Write) Die("DATA SCT write", addr, value);
        WriteData(addr, value);
        return;
    }
    if (a.reg_offset == kCtrl1) {
        WriteCtrl1(a.op, addr, value);
        return;
    }
    const int index = SpecIndex(a.reg_offset);
    if (index < 0) Die("write to unmodelled register", addr, value);
    WriteStorage(index, a.op, addr, value);
}

void Imx28Lcdif::WriteStorage(int index, Imx28SctOp op, uint32_t addr, uint32_t value) {
    const Imx28LcdifRegSpec& spec = kStorage[index];
    if (op != Imx28SctOp::Write && !spec.sct) Die("SCT write to register without SCT", addr, value);
    const uint32_t next = Imx28ApplySct(op, regs_[index], value);
    if ((next & ~spec.writable) != (spec.reset & ~spec.writable))
        Die("reserved bits", addr, value);
    if (spec.offset == kCtrl2 && (next & kCtrl2LinePatterns) != 0u)
        Die("CTRL2 line pattern", addr, value);
    regs_[index] = next;
}

/* Sections 33.4.1, 39.3 (soft reset). */
void Imx28Lcdif::WriteCtrl(Imx28SctOp op, uint32_t addr, uint32_t value) {
    const uint32_t old  = Reg(kCtrl);
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        ResetBlock(true);
        DriveIrq();
        return;
    }
    WriteStorage(SpecIndex(kCtrl), op, addr, value);
    /* Section 33.2.6: clearing RUN cancels the current transfer. */
    if ((next & kCtrlRun) == 0u) pio_left_ = 0u;
    if ((old & kCtrlRun) == 0u && (next & kCtrlRun) != 0u) Start(addr);
}

/* Section 33.4.2: IRQ status bits clear through the SCT clear address. */
void Imx28Lcdif::WriteCtrl1(Imx28SctOp op, uint32_t addr, uint32_t value) {
    const uint32_t old = Reg(kCtrl1);
    WriteStorage(SpecIndex(kCtrl1), op, addr, value);
    const uint32_t next = Reg(kCtrl1);
    if ((next & ~old & kCtrl1IrqStatus) != 0u) Die("CTRL1 IRQ status set by software", addr,
                                                    value);
    if ((next & (kCtrl1FifoClear | kCtrl1InterlaceFields | kCtrl1BusyEnable)) != 0u)
        Die("CTRL1 FIFO_CLEAR, INTERLACE_FIELDS or BUSY_ENABLE", addr, value);
    DriveIrq();
}

/* Sections 33.2.2, 33.2.6, 33.2.7. */
void Imx28Lcdif::Start(uint32_t addr) {
    const uint32_t ctrl = Reg(kCtrl);
    if ((ctrl & kCtrlUnmodelled) != 0u) Die("RUN with unmodelled CTRL format or mode", addr, ctrl);
    if ((ctrl & kCtrlWaitForVsync) != 0u &&
        ((ctrl & kCtrlVsyncMode) == 0u || (Reg(kVdctrl0) & kVdctrl0VsyncOeb) == 0u)) {
        Die("WAIT_FOR_VSYNC_EDGE without VSYNC_MODE input", addr, Reg(kVdctrl0));
    }
    if (panel_ == nullptr) Die("RUN with no MPU panel", addr, ctrl);
    const uint32_t count = Reg(kTransferCount);
    pio_left_ = (count >> 16) * (count & 0xFFFFu);
    if (pio_left_ == 0u) Die("RUN with zero TRANSFER_COUNT", addr, count);
    if ((ctrl & kCtrlMaster) != 0u) RunFrameDma();
}

/* Sections 33.2.1.1, 33.2.2: 16-bit pixels, BYTE_PACKING_FORMAT 0xF, two pixels per word. */
void Imx28Lcdif::RunFrameDma() {
    const uint32_t count   = Reg(kTransferCount);
    const uint32_t h_count = count & 0xFFFFu;
    const uint32_t v_count = count >> 16;
    const uint32_t packing = (Reg(kCtrl1) & kCtrl1Packing) >> kCtrl1PackingShift;
    if (packing != 0xFu || (h_count & 1u) != 0u) Die("frame DMA packing or odd H_COUNT",
                                                     MmioBase() + kTransferCount, count);
    const uint32_t line_bytes = h_count * 2u;
    std::vector<uint8_t> line(line_bytes);
    auto& mem = emu_.Get<EmulatedMemory>();
    uint32_t pa = Reg(kCurBuf);
    for (uint32_t y = 0; y < v_count; ++y, pa += line_bytes) {
        if (mem.TryCopyOut(pa, line.data(), line_bytes) != line_bytes)
            Die("frame DMA read outside RAM", MmioBase() + kCurBuf, pa);
        for (uint32_t x = 0; x < line_bytes; x += 2u)
            SendWord(static_cast<uint16_t>(line[x] | (line[x + 1u] << 8)));
    }
    pio_left_ = 0u;
    FinishTransfer();
}

/* Sections 33.2.6, 33.4.2 BYTE_PACKING_FORMAT: a 16-bit half-word is valid when its two
   byte bits are both set. */
void Imx28Lcdif::WriteData(uint32_t addr, uint32_t value) {
    const uint32_t ctrl = Reg(kCtrl);
    if ((ctrl & kCtrlRun) == 0u || (ctrl & kCtrlMaster) != 0u)
        Die("DATA write without RUN in PIO mode", addr, value);
    const uint32_t packing = (Reg(kCtrl1) & kCtrl1Packing) >> kCtrl1PackingShift;
    if (packing != 0x3u && packing != 0xCu && packing != 0xFu)
        Die("DATA write with unmodelled BYTE_PACKING_FORMAT", addr, packing);
    if ((packing & 0x3u) == 0x3u && pio_left_ != 0u) {
        SendWord(static_cast<uint16_t>(value & 0xFFFFu));
        --pio_left_;
    }
    if ((packing & 0xCu) == 0xCu && pio_left_ != 0u) {
        SendWord(static_cast<uint16_t>(value >> 16));
        --pio_left_;
    }
    if (pio_left_ == 0u) FinishTransfer();
}

void Imx28Lcdif::SendWord(uint16_t word) {
    if ((Reg(kCtrl) & kCtrlDataSelect) != 0u) panel_->WriteData(word);
    else                                      panel_->WriteCommand(word);
}

/* Sections 33.2.6, 33.2.7: RUN clears and cur_frame_done asserts after TRANSFER_COUNT. */
void Imx28Lcdif::FinishTransfer() {
    Reg(kCtrl) &= ~kCtrlRun;
    Reg(kCtrl1) |= kCtrl1FrameDoneIrq;
    DriveIrq();
}

/* Section 33.4.2: each IRQ bit [11:8] has its enable at [15:12]; BM_ERROR_IRQ [25] has
   BM_ERROR_IRQ_EN [26]. */
void Imx28Lcdif::DriveIrq() {
    const uint32_t ctrl1  = Reg(kCtrl1);
    const uint32_t status = ctrl1 & kCtrl1IrqStatus;
    const bool level = (status & (ctrl1 >> 4) & 0x00000F00u) != 0u ||
                       (status & (ctrl1 >> 1) & 0x02000000u) != 0u;
    if (level) icoll_->AssertSource(kIrqSource);
    else       icoll_->DeassertSource(kIrqSource);
}

}  /* namespace */

REGISTER_SERVICE(Imx28Lcdif);

}
