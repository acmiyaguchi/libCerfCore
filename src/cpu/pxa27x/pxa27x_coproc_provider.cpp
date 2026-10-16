#include "../xscale/xscale_coproc_provider_base.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/pxa27x/pxa270_id.h"
#include "../../socs/pxa27x/pxa27x_clock_manager.h"

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

/* Intel PXA27x Developer's Manual 280000-001 Section 2.2.5.4, Table 2-4
   (page 2-5). */
uint32_t CparGate(uint32_t cp_num) {
    return cp_num > 13u ? 0u : (1u << cp_num);
}

class Pxa27xCoprocProvider : public XscaleCoprocProviderBase {
public:
    using XscaleCoprocProviderBase::XscaleCoprocProviderBase;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Pxa270;
    }

protected:
    /* symbol_mk500 MK500c50BenOS013014.bin nk.exe 0x801C2258 "MRC p1,0,r1,c1,c0,0",
       0x801C2268 "ANDS r1,r1,#3", 0x801C226C "BEQ 0x801C2280". */
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num != 1u) return XscaleCoprocProviderBase::RegisterTransfer(a);
        if (a.read && a.rt == 15u) {
            return {.action = A::kUnimplemented, .gate_mask = CparGate(a.cp_num)};
        }
        if (a.read) {
            return {.action = A::kConstant, .gate_mask = CparGate(a.cp_num), .constant = 0u};
        }
        return {.action = A::kNoOp, .gate_mask = CparGate(a.cp_num)};
    }

    ArmCoprocDecision PwrmodeWrite() override { return PwrmodeRunIdleSleep(); }

    ArmCoprocDecision UnhandledCoprocessor(const ArmCoprocAccess& a) override {
        return {.action = A::kUnimplemented, .gate_mask = CparGate(a.cp_num)};
    }

    /* Intel PXA27x Developer's Manual 280000-001 Table 3-40 "Coprocessor 14
       Clocks and Power Register Summary" (page 3-105): CP14 register 6 CLKCFG. */
    ArmCoprocDecision ClkcfgTransfer(const ArmCoprocAccess& a) override {
        Pxa27xClockManager* clocks = &emu_.Get<Pxa27xClockManager>();
        if (a.read) {
            return {.action = A::kReadCallback, .context = clocks,
                    .read = &Pxa27xClockManager::ReadClkcfgHelper};
        }
        return {.action = A::kWriteCallback, .context = clocks,
                .write = &Pxa27xClockManager::WriteClkcfgHelper};
    }
};

}

REGISTER_SERVICE_AS(Pxa27xCoprocProvider, ArmCoprocProvider);
