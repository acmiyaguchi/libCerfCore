#include "../mips/mips_exception_model.h"

#include <cstdint>

#include <cerfcore/boards/board_context.h>
#include "../../socs/pr31x00/pr31500_id.h"
#include "../../socs/pr31x00/pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_cpu_state.h>
#include <cerfengine/mips/mips_mmu.h>

namespace {

/* Status mode stack (TMPR39xx-um §6.2.3): IEc<0> KUc<1> IEp<2> KUp<3> IEo<4> KUo<5>.
   The R3900 has no EXL bit; this three-level stack is the nesting mechanism. */
constexpr uint32_t kModeStackMask = 0x3Fu;
constexpr uint32_t kModePushMask  = 0x3Cu;   /* the two levels an exception shifts up */
constexpr uint32_t kStatusIEc     = 0;

/* Cause (TMPR39xx-um §6.2.1): ExcCode<6:2>. */
constexpr uint32_t kCauseExcCodeMask  = 0x7Cu;
constexpr uint32_t kCauseExcCodeShift = 2;

/* Table 6-2. BEV picks the bootstrap vectors; a UTLB (kuseg) refill takes its own. */
constexpr uint32_t kVecUtlbRefill    = 0x80000000u;
constexpr uint32_t kVecGeneral       = 0x80000080u;
constexpr uint32_t kVecUtlbRefillBev = 0xBFC00100u;
constexpr uint32_t kVecGeneralBev    = 0xBFC00180u;

/* Context: PTEBase<31:21> belongs to software, BadVPN<20:2> holds the faulting VPN
   (MIPS1_CNTXT_PTE_BASE / MIPS1_CNTXT_BAD_VPN, netbsd cpuregs.h). */
constexpr uint32_t kContextPteBase  = 0xFFE00000u;
constexpr uint32_t kContextBadVpn   = 0x001FFFFCu;
constexpr uint32_t kContextVpnShift = 10;   /* (va >> 12) << 2 */

/* EntryHi (TMPR3911 Fig 3.3.6): VPN<31:12>, PID<11:6>, reserved<5:0>. A translation
   exception loads the faulting address into it (§3.3.6.8) while leaving the PID the
   refill handler matches against. */
constexpr uint32_t kEntryHiVpnMask = 0xFFFFF000u;

class MipsExceptionR3000 final : public MipsExceptionModel {
public:
    using MipsExceptionModel::MipsExceptionModel;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Pr31500 || soc == SocId::Pr31700;
    }

    cerfengine::MipsExceptionHooks Hooks() override {
        return cerfengine::MipsExceptionHooks{this, &EnterException, &SetMmuFaultRegs,
                                              &InterruptsEnabled, &EretHook, &RfeHook};
    }

private:
    /* IEc alone: "1 = interrupt enabled" (§6.2.3). */
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void*, const cerfengine::MipsCpuState& s) {
        return (s.cp0_status & (1u << kStatusIEc)) != 0u;
    }

    static void CERFENGINE_FASTCALL EnterException(void*, cerfengine::MipsCpuState* state,
                                                   uint32_t cause, bool refill_eligible) {
        cerfengine::MipsCpuState& s = *state;

        /* EPC = the faulting PC, or the branch (pc-4) when the faulting instruction
           sits in a delay slot, which also sets Cause.BD (§6.2.2). */
        const bool in_bds = (s.branch_state != cerfengine::MipsBranch::kNone);
        s.cp0_epc = in_bds ? (s.pc - 4u) : s.pc;
        if (in_bds) {
            s.cp0_cause |= (1u << cerfengine::MipsCauseBit::kBD);
        } else {
            s.cp0_cause &= ~(1u << cerfengine::MipsCauseBit::kBD);
        }

        s.cp0_cause = (s.cp0_cause & ~kCauseExcCodeMask) |
                      ((cause << kCauseExcCodeShift) & kCauseExcCodeMask);

        /* KUp/IEp <- KUc/IEc, KUo/IEo <- KUp/IEp, then KUc = IEc = 0: kernel mode
           with interrupts disabled (§6.2.5, Fig 6-6). */
        s.cp0_status = (s.cp0_status & ~kModeStackMask) |
                       ((s.cp0_status << 2) & kModePushMask);

        /* kuseg is the mapped user segment; a refill from it vectors to UTLB Refill, one from
           a mapped kernel segment to the general vector (Table 6-2). */
        const bool utlb = refill_eligible && s.cp0_badvaddr < cerfengine::MipsSeg::kKusegEnd;
        const bool bev  = ((s.cp0_status >> cerfengine::MipsStatusBit::kBEV) & 1u) != 0u;
        if (bev) {
            s.pc = utlb ? kVecUtlbRefillBev : kVecGeneralBev;
        } else {
            s.pc = utlb ? kVecUtlbRefill : kVecGeneral;
        }

        s.branch_state = cerfengine::MipsBranch::kNone;
        s.llbit = 0;
    }

    static void CERFENGINE_FASTCALL SetMmuFaultRegs(void*, cerfengine::MipsCpuState* state,
                                                    uint32_t va) {
        cerfengine::MipsCpuState& s = *state;
        s.cp0_badvaddr = va;
        s.cp0_context  = (s.cp0_context & kContextPteBase) |
                         ((va >> kContextVpnShift) & kContextBadVpn);
        s.cp0_entryhi  = (s.cp0_entryhi & ~kEntryHiVpnMask) | (va & kEntryHiVpnMask);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(MipsExceptionR3000, MipsExceptionModel);
