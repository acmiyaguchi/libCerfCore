#include "arm_cp15_registers.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../arm_processor_config.h"

namespace cerfcore {

REGISTER_SERVICE(ArmCp15Registers);

bool ArmCp15Registers::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
}

void ArmCp15Registers::OnReady() {
    config_ = &emu_.Get<ArmProcessorConfig>();
}

/* ARM DDI 0406C.c Table B3-26 (p. B3-1418): Prefetch Abort faults update
   IFSR+IFAR only, Data Abort faults DFSR+DFAR only. B4.1.52 + Table B3-27:
   DFSR.Domain is UNKNOWN on alignment and first-level faults. */
void ArmCp15Registers::RaiseAbort(uint32_t va, uint32_t fault_status, uint32_t domain,
                                  cerfengine::ArmMmuAccess access) {
    if (access == cerfengine::ArmMmuAccess::kExecute) {
        if (config_->HasCp15V6()) {
            file_.ifsr = fault_status;
            file_.ifar = va;
            return;
        }
        /* ARM DDI 0406C.c D15.7.6 (p. D15-2623): the single ARMv4/v5 FSR "is updated
           on Prefetch Abort exceptions and Data Abort exceptions"; the FAR "is only
           updated with the MVA on Data Abort exceptions". */
        ArmDfsr fsr{};
        fsr.bits.status = fault_status;
        fsr.bits.domain = domain;
        file_.dfsr      = fsr.word;
        return;
    }
    ArmDfsr dfsr{};
    dfsr.bits.status = fault_status;
    dfsr.bits.domain = domain;
    dfsr.bits.wnr    = (access == cerfengine::ArmMmuAccess::kWrite ||
                        access == cerfengine::ArmMmuAccess::kReadWrite) ? 1u : 0u;
    file_.dfsr = dfsr.word;
    file_.dfar = va;
}

CERF_FORCEINLINE void ArmCp15Registers::RecordAlignmentFault(uint32_t va, bool is_write) {
    ArmDfsr dfsr{};
    dfsr.bits.status = ArmFaultStatus::kAlignment;
    dfsr.bits.wnr    = is_write ? 1u : 0u;
    file_.dfsr = dfsr.word;
    file_.dfar = va;
}

cerfengine::ArmResetResult ArmCp15Registers::Reset() {
    /* ARM DDI 0406C.c B4.1.153 (p. B4-1724): TTBCR is "A 32-bit RW register
       that resets to zero". */
    file_.ttbcr = 0;

    cerfengine::ArmResetResult result;
    if (pending_mmu_) {
        file_.ttbr0             = pending_ttbr0_;
        file_.dacr              = pending_dacr_;
        result.resume_mmu       = true;
        result.control_register = pending_control_;
        pending_mmu_            = false;
    }
    if (pending_aux_) {
        file_.aux_control = pending_aux_value_;
        pending_aux_      = false;
    }
    return result;
}

void ArmCp15Registers::SetPendingResumeMmu(uint32_t control, uint32_t ttbr0, uint32_t dacr) {
    pending_control_ = control;
    pending_ttbr0_   = ttbr0;
    pending_dacr_    = dacr;
    pending_mmu_     = true;
}

void ArmCp15Registers::SetPendingResumeAuxControl(uint32_t aux) {
    pending_aux_value_ = aux;
    pending_aux_       = true;
}

uint32_t CERFENGINE_FASTCALL ArmCp15Registers::ReadCcsidr(void* self) {
    ArmCp15Registers* regs = static_cast<ArmCp15Registers*>(self);
    return regs->config_->Ccsidr(regs->file_.csselr);
}

}

void CERFENGINE_FASTCALL cerfengine::host::ArmAlignmentFault(void* context, uint32_t va,
                                                             bool is_write) {
    static_cast<cerfcore::ArmCp15Registers*>(context)->RecordAlignmentFault(va, is_write);
}
