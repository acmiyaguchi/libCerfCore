#include "../vr41xx/vr41xx_bcu_impl.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "vr4121_bcu_board.h"
#include "vr4121_bcu_regs.h"
#include "vr4121_dram_decode.h"

#include <cstdint>
#include <string>
#include "vr4121_id.h"

namespace {

using cerf_vr41xx_reg_window_detail::OtherReset;
using cerf_vr41xx_reg_window_detail::ReadKind;
using cerf_vr41xx_reg_window_detail::Vr41xxBcuBase;
using cerf_vr41xx_reg_window_detail::Vr41xxRegWindowModel;
using cerf_vr41xx_reg_window_detail::WriteKind;

/* VR4121 BCU (Bus Control Unit), Internal I/O Space 2 (UM Table 1-1). The DMAAU
   block follows at 0x0B000020 (UM Table 1-2), so the BCU decodes 0x0B000000-1F. */
constexpr Vr41xxRegWindowModel kModel = {
    /*base=*/0x0B000000u,
    /*size=*/0x20u,
    16u,
    /*word_pairs=*/false,
    {
        /* 0x00 BCUCNTREG1 (UM 11.2.1): R/W D15/14/13/12/10/8/6/4/3/2/1/0, RFU-read-0 D11/9/7/5;
           RTCRST/After-reset 0 except D14 (Note 1). casio_toricomail_ce212 MMCRestore.exe
           0x12B7C RMWs it without branching (set D6 ROMWEN2, clear D10 PAGEROM2). */
        { ReadKind::kStored, WriteKind::kStored, 0xF55Fu, 0x0000u, 0x0AA0u },
        { ReadKind::kStored, WriteKind::kStored, 0x0001u, 0x0000u, 0xFFFEu },
        {},
        { ReadKind::kFatal, WriteKind::kStored, vr4121_bcu::kRamSizeWmask, 0x0000u, 0x8888u,
          OtherReset::kReset },
        {},
        {},
        { ReadKind::kFatal, WriteKind::kClear, 0x0001u, 0x0000u, 0xFFFEu },
        /* 0x0E BCURFCNTREG (UM 11.2.7): D13:0 BRF(13:0), "Number of DRAM refresh
           cycles (with TClock cycle)"; D15:14 RFU read 0. RTCRST column = BRF9; the
           After-reset row is "Value before reset is retained". */
        { ReadKind::kStored, WriteKind::kStored, 0x3FFFu, 0x0200u, 0xC000u, OtherReset::kRetain },
        {},
        {},
        {},
        /* 0x16 BCUCNTREG3 (UM 11.2.11): R/W D15:11/D7; D2:0 print "R" but UM 11.4.6 + casio_toricomail_ce212
           nk.exe 0x9F0B5B80 (`lhu;ori 7;sh`) write LCDSEL/BSEL; D10:8/D6:3 RFU read-0;
           RTCRST 0 except D14 (Note 1); After-reset row "Value before reset is retained". */
        { ReadKind::kStored, WriteKind::kStored, 0xF887u, 0x0000u, 0x0778u, OtherReset::kRetain },
        {},
        /* 0x1A SDRAMMODEREG (UM 11.2.12): R/W D15 SCLK, D6:4 LTMODE; D3 WT, D2:0 BL read
           1 and 001; D14:7 RFU read 0; RTCRST 0x8039; After-reset "retained". */
        { ReadKind::kStored, WriteKind::kStored, 0x8070u, 0x8039u, 0x7F80u, OtherReset::kRetain },
        {},
        { ReadKind::kFatal, WriteKind::kStored, 0x0F77u, 0x0944u, 0xC088u, OtherReset::kReset,
          0x0F77u },
    },
};

constexpr uint32_t kOffSdramMode = 0x1Au;
constexpr uint32_t kOffSdramCnt  = 0x1Eu;

constexpr bool ExtRomcsDefined(uint16_t value) {
    return ((value >> 12) & 0x3u) != 0x1u;
}

constexpr bool LtmodeDefined(uint16_t value) {
    const uint16_t ltmode = static_cast<uint16_t>((value >> 4) & 0x7u);
    return ltmode == 0x2u || ltmode == 0x3u;
}

constexpr bool SdramCntDefined(uint16_t value) {
    const uint16_t trc  = static_cast<uint16_t>((value >> 8) & 0xFu);
    const uint16_t tdal = static_cast<uint16_t>((value >> 4) & 0x7u);
    const uint16_t trcd = static_cast<uint16_t>(value & 0x7u);
    return trc >= 0x3u && trc <= 0x9u && tdal >= 0x2u && tdal <= 0x4u &&
           trcd >= 0x2u && trcd <= 0x4u;
}

const char* RfuEncoding(uint32_t off, uint16_t value) {
    if (off == kOffSdramMode && !LtmodeDefined(value)) {
        return "SDRAMMODEREG write with an RFU LTMODE";
    }
    if (off == kOffSdramCnt && !SdramCntDefined(value)) {
        return "SDRAMCNTREG write with an RFU TRC/TDAL/TRCD";
    }
    if (off == vr4121_bcu::kOffCnt3 && !ExtRomcsDefined(value)) {
        return "BCUCNTREG3 write with the RFU EXT_ROMCS encoding 01";
    }
    return nullptr;
}

using namespace vr4121_bcu;
using vr41xx_bcu::kOffCnt1;

constexpr uint32_t kIdxCnt1    = kOffCnt1 / 2u;
constexpr uint32_t kIdxRamSize = kOffRamSize / 2u;
constexpr uint32_t kIdxCnt3    = kOffCnt3 / 2u;

class Vr4121Bcu : public Vr41xxBcuBase<SocId::Vr4121, kModel> {
public:
    using Vr41xxBcuBase::Vr41xxBcuBase;

    void OnReady() override {
        Vr41xxBcuBase::OnReady();
        emu_.Get<Vr4121DramDecode>().Begin(StoredReg(kIdxCnt1), StoredReg(kIdxCnt3),
                                           StoredReg(kIdxRamSize));
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kModel.base;
        if (off == kOffCnt1 &&
            ((value ^ StoredReg(kIdxCnt1)) & (kCnt1Rom64 | kCnt1Rd64d)) != 0u) {
            HaltUnsupportedAccess("BCUCNTREG1 WriteHalf changes ROM64 or RD64D", addr, value);
        }
        if (off == kOffRamSize && (StoredReg(kIdxCnt1) & kCnt1Rd64d) == 0u) {
            HaltUnsupportedAccess("RAMSIZEREG WriteHalf with RD64D = 0", addr, value);
        }
        Vr41xxBcuBase::WriteHalf(addr, value);
        if (off == kOffCnt1 || off == kOffRamSize || off == kOffCnt3) CheckDecode();
    }

    void RestoreState(StateReader& r) override {
        Vr41xxBcuBase::RestoreState(r);
        const std::string why = emu_.Get<Vr4121DramDecode>().Mismatch(
            StoredReg(kIdxCnt1), StoredReg(kIdxCnt3), StoredReg(kIdxRamSize));
        if (!why.empty()) r.Reject("Vr4121Bcu: %s", why.c_str());
    }

protected:
    uint16_t ResetValue(uint32_t i, bool rtc) const override {
        const uint16_t reset = Vr41xxBcuBase::ResetValue(i, rtc);
        const bool sdram = emu_.Get<Vr4121BcuBoard>().Sdram();
        if (i == kIdxCnt1) return sdram ? static_cast<uint16_t>(reset | kCnt1Dram64) : reset;
        if (i == kIdxCnt3) return sdram ? static_cast<uint16_t>(reset | kCnt3ExtDram64) : reset;
        if (i == kIdxRamSize) return RamSizeReset(rtc);
        return reset;
    }

    void AfterReset() override {
        Vr41xxBcuBase::AfterReset();
        CheckDecode();
    }

    std::vector<Vr41xxBcuBootWrite> KernelEntryWrites() const override {
        return emu_.Get<Vr4121BcuBoard>().KernelEntryWrites();
    }

    const char* EncodingRejected(uint32_t off, uint16_t value) const override {
        return RfuEncoding(off, value);
    }

private:
    uint16_t RamSizeReset(bool rtc) const {
        auto& board = emu_.Get<Vr4121BcuBoard>();
        const auto wiring = board.DramWiring();
        if (!wiring) {
            for (const Vr41xxBcuBootWrite& w : board.KernelEntryWrites()) {
                if (w.offset == kOffRamSize) return w.value;
            }
            emu_.Get<Fatal>().Die("Vr4121Bcu: RAMSIZEREG reset depends on the DBUS32 strap, "
                                  "which this board does not declare");
        }
        const bool sdram = board.Sdram();
        const uint16_t note1 = wiring->dbus32 ? (sdram ? 3u : 1u) : (sdram ? 2u : 0u);
        const uint16_t note2 = (wiring->dbus32 && !sdram)
            ? ((StoredReg(kIdxCnt3) & kCnt3ExtDram64) ? 3u : 1u) : note1;
        const uint16_t hi = rtc ? note1 : note2;
        return static_cast<uint16_t>((hi << 12) | (hi << 8) | (note1 << 4) | note1);
    }

    void CheckDecode() const {
        emu_.Get<Vr4121DramDecode>().Check(StoredReg(kIdxCnt1), StoredReg(kIdxCnt3),
                                           StoredReg(kIdxRamSize));
    }
};

}

REGISTER_SERVICE(Vr4121Bcu);
