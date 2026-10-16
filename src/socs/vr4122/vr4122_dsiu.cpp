#include "vr4122_dsiu.h"

#include <cerfcore/boards/board_context.h>
#include "vr4122_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../vr41xx/vr41xx_icu.h"

REGISTER_SERVICE(Vr4122Dsiu);

bool Vr4122Dsiu::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Vr4122;
}

void Vr4122Dsiu::OnReady() {
    Uart16550::OnReady();
    /* VR4102 UM 7.1.4: a reset "initializes the entire internal state except for the RTC
       timer and the PMU". */
    emu_.Get<GuestCpuReset>().RegisterResetListener(
        [this](ResetLineKind) { Serial16550::Reset(); });
}

/* DSIUINTREG D11 INTDSIU (VR4131 UM 11.2.3 p193). */
void Vr4122Dsiu::SetInterruptLine(bool pending) {
    emu_.Get<Vr41xxIcu>().SetDsiuSource(pending ? (1u << 11) : 0u);
}
