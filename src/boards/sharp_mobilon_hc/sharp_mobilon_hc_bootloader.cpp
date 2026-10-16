#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/service.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/pr31x00/pr31x00_clock.h"
#include "../../socs/pr31x00/pr31x00_intc.h"
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>

namespace {

/* sharp_mobilon_hc4100_hpc2 serial.dll sub_1475B7C: UART CTL2 BAUDRATE = 0x38400 / baud - 1;
   Baud Rate = f_UARTCLK / ((BAUDRATE + 1) * 16) (TMPR3911 §16.5.2); UARTACLK 3.6864 MHz at
   CSERDIV 011 with a 9.216 MHz crystal (TMPR3911 Table 6.3.1, p.6-9). */
constexpr uint32_t kCserDiv = 3u;

/* HC-4100 nk.exe never sets INTC GLOBALEN - it RMW-preserves it (sub_91021678
   @0x910216C8 ori $t0,$t9,0x8000) and save/clears/restores it around Cmtt I/O
   (sub_910234FC @0x9102352C and $t9,$t8,0xFFFBFFFF). */
class SharpMobilonHcBootloader : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    void OnReady() override {
        Apply();
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Apply(); });
    }

private:
    /* sharp_mobilon_hc4100_hpc2 nk.exe 0x91002518-0x91002524 Clock Control lw / li 0x8000 / or / sw;
       serial.dll sub_1475404 Clock Control |= 1 (UART B) or |= 2 (UART A). */
    void Apply() {
        emu_.Get<Pr31x00Intc>().SetGlobalEnable();
        emu_.Get<Pr31x00Clock>().PresetSerialClock(kCserDiv);
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHcBootloader);
