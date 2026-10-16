#include "../mips/mips_exception_model.h"

#include <cstdint>

#include <cerfcore/boards/board_context.h>
#include "../../socs/vr4102/vr4102_id.h"
#include "../../socs/vr4111/vr4111_id.h"
#include "../../socs/vr4121/vr4121_id.h"
#include "../../socs/vr4122/vr4122_id.h"
#include "../vr5500/vr5500_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_cpu_state.h>

namespace {

class MipsExceptionR4000 final : public MipsExceptionModel {
public:
    using MipsExceptionModel::MipsExceptionModel;

    bool ShouldRegister() override {
        const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Vr4102 || soc == SocId::Vr4111 || soc == SocId::Vr4121 ||
               soc == SocId::Vr4122 || soc == SocId::Vr5500;
    }

    cerfengine::MipsExceptionHooks Hooks() override {
        return cerfengine::MipsExceptionHooks{this, &EnterException, &SetMmuFaultRegs,
                                              &InterruptsEnabled, &EretHook, &RfeHook};
    }

private:
    /* mips_cpu_hw_interrupts_enabled (internal.h): IE && !EXL && !ERL. */
    static bool CERFENGINE_FASTCALL InterruptsEnabled(void*, const cerfengine::MipsCpuState& s) {
        if ((s.cp0_status & (1u << cerfengine::MipsStatusBit::kIE)) == 0u) return false;
        if (s.cp0_status & (1u << cerfengine::MipsStatusBit::kEXL))        return false;
        if (s.cp0_status & (1u << cerfengine::MipsStatusBit::kERL))        return false;
        return true;
    }

    static void CERFENGINE_FASTCALL EnterException(void*, cerfengine::MipsCpuState* state,
                                                   uint32_t cause, bool refill_eligible) {
        cerfengine::MipsCpuState& s = *state;

        /* QEMU mips_cpu_do_interrupt EXCP_TLBL and set_EPC (tlb_helper.c). */
        const bool exl = ((s.cp0_status >> cerfengine::MipsStatusBit::kEXL) & 1u) != 0u;

        uint32_t offset = cerfengine::MipsExcVector::kOffGeneral;
        if (refill_eligible && !exl) {
            offset = cerfengine::MipsExcVector::kOffRefill;
        }

        if (!exl) {
            /* QEMU exception_resume_pc (exception.c). */
            const bool in_bds = (s.branch_state != cerfengine::MipsBranch::kNone);
            s.cp0_epc = (in_bds ? (s.pc - s.branch_len) : s.pc) | s.isa_mode;
            if (in_bds) {
                s.cp0_cause |= (1u << cerfengine::MipsCauseBit::kBD);
            } else {
                s.cp0_cause &= ~(1u << cerfengine::MipsCauseBit::kBD);
            }
            s.cp0_status |= (1u << cerfengine::MipsStatusBit::kEXL);
        }
        /* "When an exception occurs, the ISA mode bit is cleared to 0 so that the
           exception is serviced with 32-bit code" (U15509EJ2V0UM 3.4.2). */
        s.isa_mode = 0;

        const uint32_t base = ((s.cp0_status >> cerfengine::MipsStatusBit::kBEV) & 1u)
                                  ? cerfengine::MipsExcVector::kBaseBev
                                  : cerfengine::MipsExcVector::kBaseNormal;
        s.pc = base + offset;

        s.cp0_cause = (s.cp0_cause & ~(0x1Fu << cerfengine::MipsCauseBit::kExcCode)) |
                      (cause << cerfengine::MipsCauseBit::kExcCode);

        /* QEMU mips_cpu_do_interrupt (tlb_helper.c). */
        s.branch_state = cerfengine::MipsBranch::kNone;
    }

    static void CERFENGINE_FASTCALL SetMmuFaultRegs(void*, cerfengine::MipsCpuState* state,
                                                    uint32_t va) {
        /* QEMU raise_mmu_exception (tlb_helper.c); VR4102 UM
           Figure 6-1 (p.160) and VR4111 UM Figure 7-1 (p.190) Context BadVPN2. */
        cerfengine::MipsCpuState& s = *state;
        const uint32_t shift = s.min_page_shift;
        const uint32_t ctx_field = ((1u << (31u - shift)) - 1u) << 4;
        s.cp0_badvaddr = va;
        s.cp0_context  = (s.cp0_context & ~(ctx_field | 0xFu)) |
                         ((va >> (shift - 3u)) & ctx_field);
        s.cp0_entryhi  = (s.cp0_entryhi & 0xFFu) | (va & cerfengine::MipsVpn2Mask(shift));
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(MipsExceptionR4000, MipsExceptionModel);
