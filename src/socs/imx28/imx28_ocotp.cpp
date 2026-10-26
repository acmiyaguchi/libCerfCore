#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_apb_peripheral.h"
#include "imx28_id.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 chapter 20 (OCOTP) at 0x8002_C000. */
class Imx28Ocotp : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x8002C000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kWords = 40u;

    void ResetBlock();
    void ReloadShadows();
    void WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    uint32_t ctrl_ = 0u;
    uint32_t words_[kWords] = {};
};

/* Section 20.4 memory map: CTRL 0x000, DATA 0x010, 40 OTP words from CUST0 0x020 to SRK7 0x290
   at 0x10 apart, VERSION 0x2A0; only CTRL has SCT aliases (section 20.4.1). */
constexpr uint32_t kOffCtrl    = 0x000u;
constexpr uint32_t kOffData    = 0x010u;
constexpr uint32_t kOffWord0   = 0x020u;
constexpr uint32_t kWordStride = 0x010u;
constexpr uint32_t kOffVersion = 0x2A0u;
constexpr uint32_t kMapEnd     = 0x2B0u;

/* Section 20.4 memory map word order: CUST0-3, CRYPTO0-3, HWCAP0-5, SWCAP, CUSTCAP, LOCK,
   OPS0-6, ROM0-7, SRK0-7. Section 20.2.4: HWCAP0-5, SWCAP and LOCK are shadowed; section
   20.2.1: HW/SW capability, CUSTCAP, ROM and SRK shadows are writable. */
constexpr uint32_t kWordHwcap0  = 8u;
constexpr uint32_t kWordCustcap = 15u;
constexpr uint32_t kWordLock    = 16u;
constexpr uint32_t kWordRom0    = 24u;

bool IsShadowed(uint32_t word) {
    return (word >= kWordHwcap0 && word <= kWordLock) || word >= kWordRom0;
}

bool IsWritableShadow(uint32_t word) {
    return (word >= kWordHwcap0 && word <= kWordCustcap) || word >= kWordRom0;
}

/* Section 20.4.1 CTRL: WR_UNLOCK [31:16], RSRVD2 [15:14], RELOAD_SHADOWS [13], RD_BANK_OPEN [12],
   RSRVD1 [11:10], ERROR [9], BUSY [8], RSRVD0 [7:6], ADDR [5:0]. */
constexpr uint32_t kCtrlWrUnlock     = 0xFFFF0000u;
constexpr uint32_t kCtrlReloadShadow = 1u << 13;
constexpr uint32_t kCtrlRdBankOpen   = 1u << 12;
constexpr uint32_t kCtrlError        = 1u << 9;
constexpr uint32_t kCtrlAddr         = 0x0000003Fu;
constexpr uint32_t kCtrlWritable     = kCtrlWrUnlock | kCtrlReloadShadow | kCtrlRdBankOpen |
                                       kCtrlError | kCtrlAddr;

/* Section 20.2.1 step 4: a read of an unopened bank returns 0xBADA_BADA and sets ERROR. */
constexpr uint32_t kBadRead = 0xBADABADAu;

/* Section 20.4.43 VERSION reset 0105_0000h. */
constexpr uint32_t kVersion = 0x01050000u;

/* Section 20.3 loads the shadows from the fuses after reset; the fuse contents of a device are
   ungroundable, and an unprogrammed fuse reads 0. */
constexpr uint32_t kUnprogrammedFuse = 0u;

}  /* namespace */

bool Imx28Ocotp::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Ocotp::OnReady() {
    ResetBlock();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetBlock(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Imx28Ocotp::ResetBlock() {
    ctrl_ = 0u;
    for (uint32_t& w : words_) w = kUnprogrammedFuse;
}

/* Section 20.2.4: RELOAD_SHADOWS copies the fuses into the shadows and the controller clears it. */
void Imx28Ocotp::ReloadShadows() {
    for (uint32_t w = 0; w < kWords; ++w) {
        if (IsShadowed(w)) words_[w] = kUnprogrammedFuse;
    }
}

void Imx28Ocotp::Die(const char* what, uint32_t addr, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 OCOTP: %s at %08X (value %08X, CTRL %08X)", what, addr, value,
                          ctrl_);
}

/* Section 20.4.1 field descriptions; section 20.2.2 write sequence. */
void Imx28Ocotp::WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr) {
    if ((value & ~kCtrlWritable) != 0u) Die("CTRL read-only or reserved bits", addr, value);
    if (op != Imx28SctOp::Clr && (value & kCtrlWrUnlock) != 0u) {
        Die("CTRL WR_UNLOCK, OTP programming", addr, value);
    }
    if (op != Imx28SctOp::Clr && (value & kCtrlError) != 0u) Die("CTRL ERROR set by software", addr, value);
    const uint32_t keep  = ctrl_ & kCtrlError;
    uint32_t       next  = Imx28ApplySct(op, ctrl_, value) & ~kCtrlError;
    if (op == Imx28SctOp::Clr) next |= keep & ~value;
    else                       next |= keep;
    if ((next & kCtrlReloadShadow) != 0u) {
        ReloadShadows();
        next &= ~kCtrlReloadShadow;
    }
    ctrl_ = next;
}

uint32_t Imx28Ocotp::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.op != Imx28SctOp::Write) {
        if (a.reg_offset != kOffCtrl) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    if (a.reg_offset == kOffCtrl) return ctrl_;
    if (a.reg_offset == kOffData) return 0u;
    if (a.reg_offset == kOffVersion) return kVersion;
    const uint32_t word = (a.reg_offset - kOffWord0) / kWordStride;
    if (!IsShadowed(word) && (ctrl_ & kCtrlRdBankOpen) == 0u) {
        ctrl_ |= kCtrlError;
        return kBadRead;
    }
    return words_[word];
}

void Imx28Ocotp::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    if (a.reg_offset == kOffCtrl) {
        WriteCtrl(a.op, value, addr);
        return;
    }
    if (a.op != Imx28SctOp::Write) HaltUnsupportedAccess("WriteWord", addr, value);
    if (a.reg_offset == kOffData) Die("DATA write, OTP programming", addr, value);
    if (a.reg_offset == kOffVersion) Die("VERSION write", addr, value);
    const uint32_t word = (a.reg_offset - kOffWord0) / kWordStride;
    if (!IsWritableShadow(word)) Die("write to a read-only OTP word", addr, value);
    words_[word] = value;
}

void Imx28Ocotp::SaveState(StateWriter& w) {
    w.Write("ctrl", ctrl_);
    w.Write("words", words_);
}

void Imx28Ocotp::RestoreState(StateReader& r) {
    r.Read("ctrl", ctrl_);
    r.Read("words", words_);
}

REGISTER_SERVICE(Imx28Ocotp);

}
