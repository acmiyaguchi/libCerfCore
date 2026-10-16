#include "arm_coproc_provider.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "arm_cp15_decisions.h"

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using cerfengine::ArmCoprocKind;

void ArmCoprocProvider::OnReady() {
    cp15_ = &emu_.Get<ArmCp15Decisions>();
}

ArmCoprocDecision ArmCoprocProvider::Decide(const ArmCoprocAccess& a) {
    switch (a.kind) {
    case ArmCoprocKind::kRegisterTransfer: {
        ArmCoprocDecision d = RegisterTransfer(a);
        d.user_allowed = cp15_->UserAllowed(a);
        return d;
    }
    case ArmCoprocKind::kRegisterTransferDouble:
        return RegisterTransferDouble(a);
    case ArmCoprocKind::kDataTransfer:
        return DataTransfer(a);
    case ArmCoprocKind::kDataOperation:
        return DataOperation(a);
    }
    emu_.Get<Fatal>().Die("ArmCoprocProvider: access kind %u",
                          static_cast<uint32_t>(a.kind));
}

ArmCoprocDecision ArmCoprocProvider::RegisterTransferDouble(const ArmCoprocAccess&) {
    return {.action = cerfengine::ArmCoprocAction::kUndefined};
}
