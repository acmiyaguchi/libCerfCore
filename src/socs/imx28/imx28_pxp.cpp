#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_pxp_frame.h"
#include "imx28_pxp_regs.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

namespace {

using namespace imx28_pxp;

/* MCIMX28RM Rev. 2 Table 5-1: source 39 pxp_irq. */
constexpr uint32_t kIrqSource = 39u;

/* Section 34.2.12 Table 34-3 word order. */
constexpr uint32_t kReloadHead[16] = {kCtrl,  0x020u, 0x030u, 0x040u, 0x050u, 0x060u,
                                      0x070u, 0x080u, 0x090u, 0x0A0u, 0x0B0u, 0x0C0u,
                                      0x180u, 0x190u, 0x1A0u, 0x1B0u};

uint32_t ReloadOffset(uint32_t word) {
    return word < 16u ? kReloadHead[word] : kOl0 + (word - 16u) * 0x10u;
}

/* MCIMX28RM Rev. 2 chapter 34 (PXP) at 0x8002_A000. */
class Imx28Pxp : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx28;
    }

    void OnReady() override {
        icoll_ = &emu_.Get<Imx28Icoll>();
        ResetBlock();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ResetBlock();
            DriveIrq();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x8002A000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override { w.Write("regs", regs_); }
    void RestoreState(StateReader& r) override { r.Read("regs", regs_); }
    void PostRestore() override { DriveIrq(); }

private:
    uint32_t& Reg(uint32_t offset) { return regs_[Slot(offset)]; }

    void ResetBlock();
    void WriteCtrl(Imx28SctOp op, uint32_t addr, uint32_t value);
    void WriteStat(Imx28SctOp op, uint32_t addr, uint32_t value);
    void WriteNext(Imx28SctOp op, uint32_t addr, uint32_t value);
    void WriteStorage(uint32_t offset, Imx28SctOp op, uint32_t addr, uint32_t value);
    void LoadAndRun(uint32_t pointer);
    void DriveIrq();
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    Imx28Icoll*   icoll_ = nullptr;
    Imx28PxpFrame frame_{emu_};
    uint32_t      regs_[kSlots] = {};
};

void Imx28Pxp::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 PXP: %s at %08X (value %08X, CTRL %08X)", what, addr, value,
                          regs_[Slot(kCtrl)]);
}

void Imx28Pxp::ResetBlock() {
    for (uint32_t s = 0; s < kSlots; ++s) regs_[s] = SpecAt(s << 4).reset;
}

uint32_t Imx28Pxp::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kSlots * 0x10u || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) HaltUnsupportedAccess("ReadWord", addr, 0);
    if (a.reg_offset == kDebug) Die("DEBUG read", addr, 0u);
    const RegSpec spec = SpecAt(a.reg_offset);
    if (spec.reset == 0u && spec.writable == 0u && a.reg_offset < kOl0) Die("read of unmapped "
                                                                            "offset", addr, 0u);
    return regs_[Slot(a.reg_offset)];
}

void Imx28Pxp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kSlots * 0x10u || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr,
                                                                            value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kCtrl) {
        WriteCtrl(a.op, addr, value);
        return;
    }
    RequireOperating("register write", Reg(kCtrl), addr, value);
    if (a.reg_offset == kStat) {
        WriteStat(a.op, addr, value);
        return;
    }
    if (a.reg_offset == kNext) {
        WriteNext(a.op, addr, value);
        return;
    }
    /* Section 34.4: only CTRL, STAT and NEXT have SCT addresses. */
    if (a.op != Imx28SctOp::Write) Die("SCT write to register without SCT", addr, value);
    WriteStorage(a.reg_offset, a.op, addr, value);
}

void Imx28Pxp::WriteStorage(uint32_t offset, Imx28SctOp op, uint32_t addr, uint32_t value) {
    const RegSpec  spec = SpecAt(offset);
    const uint32_t next = Imx28ApplySct(op, Reg(offset), value);
    if ((next & ~spec.writable) != (spec.reset & ~spec.writable))
        Die("read-only or reserved bits", addr, value);
    Reg(offset) = next;
}

/* Sections 34.4.1, 39.3 (soft reset). */
void Imx28Pxp::WriteCtrl(Imx28SctOp op, uint32_t addr, uint32_t value) {
    const uint32_t old  = Reg(kCtrl);
    const uint32_t next = Imx28ApplySct(op, old, value);
    if (IsSoftReset(old, next)) {
        ResetBlock();
        DriveIrq();
        return;
    }
    if ((next & kCtrlEnable) != (old & kCtrlEnable)) Die("CTRL ENABLE written", addr, value);
    WriteStorage(kCtrl, op, addr, value);
    DriveIrq();
}

/* Section 34.4.2: the ROM acknowledges through STAT_CLR. */
void Imx28Pxp::WriteStat(Imx28SctOp op, uint32_t addr, uint32_t value) {
    if (op != Imx28SctOp::Clr) Die("STAT write other than CLR", addr, value);
    Reg(kStat) &= ~value;
    DriveIrq();
}

/* Section 34.4.17: a write while idle loads the pointer at once; CLR cancels the reload; SET
   and TOG are not to be used. */
void Imx28Pxp::WriteNext(Imx28SctOp op, uint32_t addr, uint32_t value) {
    if (op == Imx28SctOp::Clr) {
        Reg(kNext) &= ~(value | kNextEnabled);
        return;
    }
    if (op != Imx28SctOp::Write) Die("NEXT SET or TOG", addr, value);
    if ((value & 0x3u) != 0u) Die("NEXT pointer not word aligned", addr, value);
    if ((Reg(kCtrl) & kCtrlEnable) != 0u) Die("NEXT write while a frame runs", addr, value);
    Reg(kNext) = value | kNextEnabled;
    LoadAndRun(value);
}

/* Sections 34.2.12, 34.4.17: STAT, CSCCOEFFn, NEXT and VERSION are not reloaded; NEXT_IRQ
   follows the load, IRQ and ENABLE clear follow the frame (section 34.4.1 ENABLE). */
void Imx28Pxp::LoadAndRun(uint32_t pointer) {
    uint32_t words[kReloadWords] = {};
    const uint32_t bytes = kReloadWords * 4u;
    if (emu_.Get<EmulatedMemory>().TryCopyOut(pointer, reinterpret_cast<uint8_t*>(words),
                                              bytes) != bytes) {
        Die("NEXT structure outside RAM", MmioBase() + kNext, pointer);
    }
    for (uint32_t i = 0; i < kReloadWords; ++i) {
        const uint32_t offset = ReloadOffset(i);
        const RegSpec  spec   = SpecAt(offset);
        if ((words[i] & ~spec.writable) != (spec.reset & ~spec.writable))
            Die("NEXT structure reserved bits", pointer + i * 4u, words[i]);
        Reg(offset) = words[i];
    }
    Reg(kNext) &= ~kNextEnabled;
    Reg(kStat) |= kStatNextIrq;
    if ((Reg(kCtrl) & kCtrlEnable) == 0u) Die("NEXT structure CTRL without ENABLE", pointer,
                                              Reg(kCtrl));
    frame_.Render(regs_);
    Reg(kCtrl) &= ~kCtrlEnable;
    Reg(kStat) |= kStatIrq;
    DriveIrq();
}

/* Section 34.4.1 IRQ_ENABLE and NEXT_IRQ_ENABLE; section 34.4.2 IRQ routed through pxp_irq. */
void Imx28Pxp::DriveIrq() {
    const uint32_t ctrl = Reg(kCtrl);
    const uint32_t stat = Reg(kStat);
    const bool level = ((stat & kStatIrq) != 0u && (ctrl & kCtrlIrqEnable) != 0u) ||
                       ((stat & kStatNextIrq) != 0u && (ctrl & kCtrlNextIrqEnable) != 0u);
    if (level) icoll_->AssertSource(kIrqSource);
    else       icoll_->DeassertSource(kIrqSource);
}

}  /* namespace */

REGISTER_SERVICE(Imx28Pxp);

}
