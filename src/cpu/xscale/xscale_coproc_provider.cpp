#include "xscale_coproc_provider_base.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pxa255/pxa255_clock_manager.h"
#include "../../socs/pxa255/pxa255_id.h"

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

class XscaleCoprocProvider : public XscaleCoprocProviderBase {
public:
    using XscaleCoprocProviderBase::XscaleCoprocProviderBase;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Pxa255;
    }

protected:
    /* PXA255 Dev Man Table 3-25 PWRMODE M [1:0]: "00 - Run/turbo mode", "01 -
       Idle mode", "10 - reserved", "11 - Sleep mode"; [31:2] reserved. */
    ArmCoprocDecision PwrmodeWrite() override { return PwrmodeRunIdleSleep(); }

    /* PXA255 Dev Man Table 3-23: "Read CCLKCFG MRC p14, 0, Rd, c6, c0, 0",
       "Enter turbo mode ... MCR p14, 0, Rd, c6, c0, 0". */
    ArmCoprocDecision ClkcfgTransfer(const ArmCoprocAccess& a) override {
        Pxa255ClockManager* clocks = &emu_.Get<Pxa255ClockManager>();
        if (a.read) {
            return {.action = A::kReadCallback, .context = clocks,
                    .read = &Pxa255ClockManager::ReadClkcfgHelper};
        }
        return {.action = A::kWriteCallback, .context = clocks,
                .write = &Pxa255ClockManager::WriteClkcfgHelper};
    }

    ArmCoprocDecision UnhandledCoprocessor(const ArmCoprocAccess&) override {
        return {.action = A::kUndefined};
    }
};

}

REGISTER_SERVICE_AS(XscaleCoprocProvider, ArmCoprocProvider);
