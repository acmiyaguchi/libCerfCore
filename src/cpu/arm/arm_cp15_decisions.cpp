#include "arm_cp15_decisions.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../arm_processor_config.h"
#include "arm_cp15_registers.h"

namespace cerfcore {

REGISTER_SERVICE(ArmCp15Decisions);

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using cerfengine::ArmTlbTarget;
using A = cerfengine::ArmCoprocAction;

}

bool ArmCp15Decisions::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
}

void ArmCp15Decisions::OnReady() {
    regs_   = &emu_.Get<ArmCp15Registers>();
    config_ = &emu_.Get<ArmProcessorConfig>();
}

ArmCoprocDecision ArmCp15Decisions::Decide(const ArmCoprocAccess& a) const {
    ArmCp15File& f = regs_->File();
    const bool v6 = config_->HasCp15V6();

    switch (a.crn) {
    case 0:
        return Identification(a);

    case 1:
        return SystemControl(a);

    case 2:
        if (v6 && (a.opc2 == 1u || a.opc2 == 2u)) {
            return {.action = A::kRegister, .reg = a.opc2 == 1u ? &f.ttbr1 : &f.ttbcr};
        }
        return {.action = A::kRegisterContextSwitch, .reg = &f.ttbr0};

    case 3:
        return {.action = A::kRegisterFlushTlbs, .reg = &f.dacr};

    case 5:
        return FaultRegister(a, 1u, &f.ifsr, &f.dfsr);

    case 6:
        return FaultRegister(a, 2u, &f.ifar, &f.dfar);

    case 7:
        return CacheOperation(a);

    case 8:
        return TlbOperation(a);

    case 9:
        if (config_->HasL2CacheAuxControl() && a.opc1 == 1u && a.crm == 0u && a.opc2 == 2u) {
            return {.action = A::kRegister, .reg = &f.l2_aux_control};
        }
        return {.action = A::kUndefined};

    case 10:
        if (v6 && a.opc1 == 0u && a.crm == 2u && (a.opc2 == 0u || a.opc2 == 1u)) {
            return {.action = A::kRegister, .reg = a.opc2 == 0u ? &f.prrr : &f.nmrr};
        }
        return {.action = A::kUndefined};

    case 12:
        if (config_->HasSecurityExtensions()) return {.action = A::kUnimplemented};
        return {.action = A::kUndefined};

    case 13:
        return ProcessId(a);

    case 15:
        if (config_->HasL1SystemArrayDebug() && !a.read && a.opc1 == 0u && a.crm == 2u &&
            a.opc2 == 4u) {
            return {.action = A::kNoOp};
        }
        return {.action = A::kUnimplemented};

    default:
        return {.action = A::kUndefined};
    }
}

ArmCoprocDecision ArmCp15Decisions::Identification(const ArmCoprocAccess& a) const {
    const bool v7 = config_->HasCp15V7();
    if (v7 && a.opc1 == 1u && a.crm == 0u && a.read) {
        if (a.opc2 == 0u) {
            return {.action  = A::kReadCallback,
                    .context = regs_,
                    .read    = &ArmCp15Registers::ReadCcsidr};
        }
        if (a.opc2 == 1u) return {.action = A::kConstant, .constant = config_->Clidr()};
        return {.action = A::kUndefined};
    }
    if (v7 && a.opc1 == 2u && a.crm == 0u && a.opc2 == 0u) {
        return {.action = A::kRegister, .reg = &regs_->File().csselr};
    }
    if (a.opc1 == 0u && (a.crm == 0u || !config_->HasCp15V6())) {
        if (!a.read) return {.action = A::kUndefined};
        if (a.opc2 == 0u) return {.action = A::kConstant, .constant = config_->Midr()};
        if (a.opc2 == 1u) return {.action = A::kConstant, .constant = config_->Ctr()};
        return {.action = A::kUndefined};
    }
    return {.action = A::kUndefined};
}

ArmCoprocDecision ArmCp15Decisions::SystemControl(const ArmCoprocAccess& a) const {
    const bool v6      = config_->HasCp15V6();
    const bool has_aux = config_->HasAuxControlRegister();
    if (a.opc1 == 0u && a.crm == 1u && config_->HasSecurityExtensions()) {
        return {.action = A::kUnimplemented};
    }
    if (a.opc1 != 0u || (a.crm != 0u && v6)) return {.action = A::kUndefined};

    const uint32_t op2 = (has_aux || v6) ? a.opc2 : 0u;
    if (op2 == 0u) return {.action = A::kSystemControl};
    if (op2 == 1u && has_aux) {
        return {.action = A::kRegister, .reg = &regs_->File().aux_control};
    }
    if (op2 == 2u && v6) return {.action = A::kCoprocessorAccess};
    return {.action = A::kUndefined};
}

ArmCoprocDecision ArmCp15Decisions::FaultRegister(const ArmCoprocAccess& a, uint32_t v6_opc2,
                                                  uint32_t* v6_reg,
                                                  uint32_t* legacy_reg) const {
    const bool v6     = config_->HasCp15V6();
    const bool is_v6r = a.opc2 == v6_opc2 && v6;
    if (a.opc1 != 0u || (a.crm != 0u && v6) || (a.opc2 != 0u && !is_v6r && v6)) {
        return {.action = A::kUndefined};
    }
    return {.action = A::kRegister, .reg = is_v6r ? v6_reg : legacy_reg};
}

ArmCoprocDecision ArmCp15Decisions::ProcessId(const ArmCoprocAccess& a) const {
    ArmCp15File& f = regs_->File();
    if (config_->HasCp15V6() && a.opc2 >= 1u && a.opc2 <= 4u) {
        switch (a.opc2) {
        case 1:  return {.action = A::kContextId};
        case 2:  return {.action = A::kRegister, .reg = &f.tpidrurw};
        case 3:  return {.action = A::kRegister, .reg = &f.tpidruro};
        default: return {.action = A::kRegister, .reg = &f.tpidrprw};
        }
    }
    return {.action = A::kFcsePid};
}

ArmCoprocDecision ArmCp15Decisions::CacheRead(const ArmCoprocAccess& a) const {
    const bool v7 = config_->HasCp15V7();
    const bool v6 = config_->HasCp15V6();
    if ((a.crm == 10u || a.crm == 14u) && a.opc2 == 3u && !v7) {
        return {.action = A::kUnimplemented};
    }
    if (a.crm == 10u && a.opc2 == 6u && v6 && !v7) {
        return {.action = A::kConstant, .constant = 0u};
    }
    if ((v6 || v7) && (a.crm == 4u || a.crm == 8u)) return {.action = A::kUnimplemented};
    return {.action = A::kUndefined};
}

ArmCoprocDecision ArmCp15Decisions::CacheOperation(const ArmCoprocAccess& a) const {
    const bool v7 = config_->HasCp15V7();
    const bool v6 = config_->HasCp15V6();
    if (a.opc1 != 0u) return {.action = A::kUndefined};
    if (a.read) return CacheRead(a);

    const uint32_t op2 = a.opc2;
    switch (a.crm) {
    case 0:
        if (op2 == 4u && !v7) return {.action = A::kWaitForInterrupt};
        if (op2 == 0u && !v6 && !v7) return {.action = A::kInvalidateCode};
        break;
    case 5:
        if (op2 == 0u || op2 == 1u || (op2 == 2u && !v7)) return {.action = A::kInvalidateCode};
        if (op2 == 4u || op2 == 6u || op2 == 7u) return {.action = A::kNoOp};
        break;
    case 6:
        if (op2 == 1u || op2 == 2u || (op2 == 0u && !v7)) return {.action = A::kNoOp};
        break;
    case 7:
        if (!v7 && op2 <= 2u) return {.action = A::kInvalidateCode};
        break;
    case 4:
    case 8:
        if (v6 || v7) return {.action = A::kUnimplemented};
        break;
    case 10:
        if (op2 == 1u || op2 == 2u || (op2 == 0u && v6 && !v7)) return {.action = A::kNoOp};
        if (op2 == 4u || op2 == 5u) return {.action = A::kNoOp};
        break;
    case 11:
        if (v7 ? (op2 == 1u) : (op2 <= 2u)) return {.action = A::kNoOp};
        break;
    case 13:
        if (op2 == 1u && !v7) return {.action = A::kNoOp};
        break;
    case 14:
        if (op2 == 1u || op2 == 2u || (op2 == 0u && v6 && !v7)) return {.action = A::kNoOp};
        break;
    case 15:
        if (!v7 && (op2 == 1u || op2 == 2u || (op2 == 0u && !v6))) {
            return {.action = A::kInvalidateCode};
        }
        break;
    default:
        break;
    }
    return {.action = A::kUndefined};
}

ArmCoprocDecision ArmCp15Decisions::TlbOperation(const ArmCoprocAccess& a) const {
    const bool v6plus = config_->HasCp15V6() || config_->HasCp15V7();
    if (a.read || a.opc1 != 0u) return {.action = A::kUndefined};

    ArmTlbTarget target;
    switch (a.crm) {
    case 5:  target = ArmTlbTarget::kInstruction; break;
    case 6:  target = ArmTlbTarget::kData;        break;
    case 7:  target = ArmTlbTarget::kUnified;     break;
    default: return {.action = A::kUndefined};
    }

    if (a.opc2 == 0u || (a.opc2 == 2u && v6plus)) {
        return {.action = A::kInvalidateTlbAll, .tlb_target = target};
    }
    if (a.opc2 == 1u) return {.action = A::kInvalidateTlbByMva, .tlb_target = target};
    return {.action = A::kUndefined};
}

bool ArmCp15Decisions::UserAllowed(const ArmCoprocAccess& a) const {
    if (a.kind != cerfengine::ArmCoprocKind::kRegisterTransfer || a.cp_num != 15u ||
        a.opc1 != 0u) {
        return false;
    }
    const bool v6 = config_->HasCp15V6();
    const bool v7 = config_->HasCp15V7();
    const bool barrier = v6 && !a.read && a.crn == 7u &&
        ((a.crm == 5u && a.opc2 == 4u) || (a.crm == 10u && (a.opc2 == 4u || a.opc2 == 5u)));
    const bool thread_id = v7 && a.crn == 13u && a.crm == 0u &&
        (a.opc2 == 2u || (a.opc2 == 3u && a.read));
    const bool block_mgmt = v6 && !v7 && a.crn == 7u && a.crm == 12u &&
        ((a.opc2 == 4u && a.read) || (a.opc2 == 5u && !a.read));
    return barrier || thread_id || block_mgmt;
}

}
