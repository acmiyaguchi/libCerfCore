#include "wm9713_clocks.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/symbol_hamptons/symbol_hamptons_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "wm9713_master_clocks.h"

#include <algorithm>

namespace {

/* Cirrus Logic WM9713L Rev 4.0 Table 3 (page 22): 44h SEXT[6:4] 14:12, CLKSRC 7 "1 = External clock",
   CLKBX2 2, CLKAX2 1, CLKMUX 0 "1 = Use MCLKB". */
constexpr uint16_t kClkSrc = 1u << 7, kClkBx2 = 1u << 2, kClkAx2 = 1u << 1, kClkMux = 1u << 0;
/* Cirrus Logic WM9713L Rev 4.0 Table 4 (page 23): 46h N[3:0] 15:12, LF 11, SDM 10, DIVSEL 9, DIVCTL 8,
   PGADDR 6:4, PGDATA 3:0. */
constexpr uint16_t kLf = 1u << 11, kSdm = 1u << 10, kDivSel = 1u << 9, kDivCtl = 1u << 8;
/* Cirrus Logic WM9713L Rev 4.0 page 24: "Fractional N mode provides a divide resolution of 1/2^22". */
constexpr uint64_t kFractionOne = 1u << 22;
constexpr uint32_t kKMask       = 0x003FFFFFu;

}  // namespace

bool Wm9713Clocks::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SymbolHamptons;
}

void Wm9713Clocks::OnReady() { inputs_ = &emu_.Get<Wm9713MasterClocks>(); }

/* Cirrus Logic WM9713L Rev 4.0 Table 6 (page 25): page 111 SPLL[6:4] bits 31:28, page 110 SPLL[3:0] bits 27:24,
   K[21:0] from page 101 bits 21:20, a "control word for fractional N division". */
void Wm9713Clocks::WritePager(uint16_t r46) {
    const uint32_t shift = ((r46 >> 4) & 7u) * 4u;
    pager_ = (pager_ & ~(0xFu << shift)) | (static_cast<uint32_t>(r46 & 0xFu) << shift);
}

/* Cirrus Logic WM9713L Rev 4.0 Figure 10 (page 21): MCLKA and MCLKB through x2 and CLK_MUX to SYSCLK, CLKSRC
   selects SYSCLK or the PLL as AC97 CLK, DIV 2 to BCLK; Figure 11 (page 23): MCLK through DIV 4/2/1 to PLL_IN,
   DIV 4 from PLL_OUT to CLK_OUT; page 24: PLL_OUT / PLL_IN = N, or N + K / 2^22 with SDM, LF a further 4. */
GuestCycleClock::Rate Wm9713Clocks::BitClock(uint16_t r44, uint16_t r46, bool pll_on) const {
    const bool     use_b  = (r44 & kClkMux) != 0u;
    const bool     x2     = (r44 & (use_b ? kClkBx2 : kClkAx2)) != 0u;
    const uint64_t sysclk = (use_b ? inputs_->MclkbHz() : inputs_->MclkaHz()) * (x2 ? 2u : 1u);
    if ((r44 & kClkSrc) != 0u) return GuestCycleClock::Rate{sysclk, 2u}.Reduced();
    if (!pll_on) return GuestCycleClock::Rate{0u, 1u};
    const uint64_t prediv = (r46 & kDivSel) == 0u ? 1u : ((r46 & kDivCtl) != 0u ? 4u : 2u);
    const uint64_t n      = std::max<uint64_t>(static_cast<uint64_t>(r46 >> 12), 1u);
    const uint64_t k      = (r46 & kSdm) != 0u ? (pager_ & kKMask) : 0u;
    const uint64_t lf     = (r46 & kLf) != 0u ? 4u : 1u;
    return GuestCycleClock::Rate{sysclk * (n * kFractionOne + k) * lf, prediv * kFractionOne * 4u * 2u}.Reduced();
}

/* Cirrus Logic WM9713L Rev 4.0 page 20: "If the PLL is selected (CLK_SRC = 0), S[6:0] = SPLL[6:0]", with an
   external clock "S[6:0] = SEXT[6:0]"; Table 3 (page 22) S[6:4] "000 = f ... 111 = f/8". */
uint32_t Wm9713Clocks::HifiDivider(uint16_t r44) const {
    const uint32_t s = (r44 & kClkSrc) != 0u ? (r44 >> 12) & 7u : (pager_ >> 28) & 7u;
    return s + 1u;
}

void Wm9713Clocks::Save(StateWriter& w) const { w.Write<uint32_t>("wm9713_pager", pager_); }

void Wm9713Clocks::Restore(StateReader& r) { r.Read("wm9713_pager", pager_); }

REGISTER_SERVICE(Wm9713Clocks);
