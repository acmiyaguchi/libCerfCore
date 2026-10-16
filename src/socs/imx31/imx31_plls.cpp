#include "imx31_plls.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "imx31_clock_input.h"
#include "imx31_id.h"

namespace {

/* MCIMX31RM Table 3-4 CCMR: MPE [3], UPE [9], SPE [8], PRCS [2:1] (01 FPM, 10 CKIH).
   §3.2.1.2: the FPM output "equals 1024*(CKIL frequency)". */
constexpr uint32_t kCcmrPrcsShift  = 1;
constexpr uint32_t kPrcsFpm        = 1u;
constexpr uint32_t kPrcsCkih       = 2u;
constexpr uint64_t kFpmMultiplier  = 1024u;
constexpr uint32_t kEnableBits[]   = {1u << 3, 1u << 9, 1u << 8};
/* Table 3-4 FPME [0]: "This bit defines if the FPM will be enabled. This applies even if it was
   not selected as reference clock source." */
constexpr uint32_t kCcmrFpme       = 1u << 0;
/* Table 3-8 / Table 3-9: a new value in PD [29:26], MFD [25:16], MFI [13:10] or MFN [9:0]
   loses the lock; "after a freq. lock time delay ... the PLL re-locks". */
constexpr uint32_t kFreqFields = 0x3FFF3FFFu;

}

bool Imx31Plls::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx31;
}

void Imx31Plls::OnReady() {
    input_ = &emu_.Get<Imx31ClockInput>();
}

uint32_t Imx31Plls::Prcs(uint32_t ccmr) {
    return (ccmr >> kCcmrPrcsShift) & 0x3u;
}

void Imx31Plls::Settle(uint32_t ccmr, const Controls& ctl) {
    for (uint32_t p = 0; p < kCount; ++p) {
        ctl_[p] = ctl[p];
        on_[p]  = (ccmr & kEnableBits[p]) != 0u ? 1u : 0u;
    }
    prcs_ = static_cast<uint8_t>(Prcs(ccmr));
}

void Imx31Plls::OnCcmrWrite(uint32_t old, uint32_t value, const Controls& ctl) {
    const uint32_t want = Prcs(value);
    if (want != Prcs(old) && (old & kEnableBits[0]) != 0u) {
        emu_.Get<Fatal>().Die("Imx31Plls: CCMR 0x%08X changes PRCS while the MCU PLL is enabled",
                              value);
    }
    if (want != prcs_) {
        if (want == kPrcsFpm) {
            emu_.Get<Fatal>().Die("Imx31Plls: CCMR 0x%08X switches the PLL reference to the FPM; "
                                  "the FPM ready time is not modeled", value);
        }
        if (want != kPrcsCkih) {
            emu_.Get<Fatal>().Die("Imx31Plls: CCMR 0x%08X selects the reserved PLL reference %u",
                                  value, want);
        }
    }
    if ((value & kCcmrFpme) == 0u && prcs_ == kPrcsFpm) {
        emu_.Get<Fatal>().Die("Imx31Plls: CCMR 0x%08X disables the FPM while it is the PLL "
                              "reference", value);
    }
    for (uint32_t p = 0; p < kCount; ++p) {
        const uint32_t bit = kEnableBits[p];
        if ((old & bit) != 0u && (value & bit) == 0u) {
            on_[p]  = 0u;
            ctl_[p] = ctl[p];
        } else if ((old & bit) == 0u && (value & bit) != 0u) {
            Lock(p, ctl[p]);
        }
    }
    if (want != prcs_) SwitchReference(want);
}

void Imx31Plls::OnControlWrite(uint32_t pll, uint32_t old, uint32_t value, uint32_t ccmr) {
    if (((old ^ value) & kFreqFields) == 0u) return;
    if ((ccmr & kEnableBits[pll]) == 0u) {
        ctl_[pll] = value;
        return;
    }
    Lock(pll, value);
}

void Imx31Plls::Lock(uint32_t pll, uint32_t ctl) {
    on_[pll]  = 1u;
    ctl_[pll] = ctl;
    LOG(SocClkpwr, "Imx31Plls: PLL %u locks to control 0x%08X\n", pll, ctl);
}

void Imx31Plls::SwitchReference(uint32_t prcs) {
    for (uint32_t p = 0; p < kCount; ++p) {
        if (on_[p] != 0u) {
            emu_.Get<Fatal>().Die("Imx31Plls: PLL %u is enabled when the PLL reference switches "
                                  "to %u; the PLL output across a reference switch is not "
                                  "modeled", p, prcs);
        }
    }
    prcs_ = static_cast<uint8_t>(prcs);
    LOG(SocClkpwr, "Imx31Plls: PLL reference switches to %llu Hz\n",
        static_cast<unsigned long long>(RefHz()));
}

/* Figure 3-24: pll_ref_clk is CKIH or the Frequency Pre-Multiplier output, chosen
   by CCMR PRCS (Table 3-4). */
uint64_t Imx31Plls::RefHz() const {
    if (prcs_ == kPrcsCkih) return input_->CkihHz();
    if (prcs_ == kPrcsFpm)  return input_->CkilHz() * kFpmMultiplier;
    emu_.Get<Fatal>().Die("Imx31Plls: the PLL reference select %u is reserved",
                          static_cast<unsigned>(prcs_));
}

/* Eqn 3-1: Fvco = Fref x 2 x (MFI + MFN/MFD) / PD. Field encodings per Table 3-8
   (MPCTL) / Table 3-9 (UPCTL): PD and MFD are stored biased by one, MFI saturates
   up to 5, MFN is a signed 10-bit two's-complement numerator. */
uint64_t Imx31Plls::LockedHz(uint32_t pll) const {
    const uint32_t ctl = ctl_[pll];
    const int64_t  pd  = ((ctl >> 26) & 0xFu) + 1u;
    const int64_t  mfd = ((ctl >> 16) & 0x3FFu) + 1u;
    int64_t mfi = (ctl >> 10) & 0xFu;
    if (mfi < 5) mfi = 5;
    int64_t mfn = static_cast<int64_t>(ctl & 0x3FFu);
    if (mfn & 0x200) mfn -= 0x400;
    const int64_t num = 2 * static_cast<int64_t>(RefHz()) * (mfi * mfd + mfn);
    const int64_t den = mfd * pd;
    if (num <= 0 || num % den != 0) {
        emu_.Get<Fatal>().Die("Imx31Plls: PLL %u control 0x%08X on the %llu Hz reference is not "
                              "a whole number of Hz", pll, ctl,
                              static_cast<unsigned long long>(RefHz()));
    }
    return static_cast<uint64_t>(num / den);
}

uint64_t Imx31Plls::OutputHz(uint32_t pll, uint64_t off_hz) const {
    return on_[pll] != 0u ? LockedHz(pll) : off_hz;
}

void Imx31Plls::SaveState(StateWriter& w) {
    w.WriteBytes("pll_ctl", ctl_.data(), sizeof(ctl_));
    w.WriteBytes("pll_on", on_.data(), sizeof(on_));
    w.Write<uint8_t>("pll_ref_prcs", prcs_);
}

void Imx31Plls::RestoreState(StateReader& r) {
    r.ReadBytes("pll_ctl", ctl_.data(), sizeof(ctl_));
    r.ReadBytes("pll_on", on_.data(), sizeof(on_));
    r.Read("pll_ref_prcs", prcs_);
}

REGISTER_SERVICE(Imx31Plls);
