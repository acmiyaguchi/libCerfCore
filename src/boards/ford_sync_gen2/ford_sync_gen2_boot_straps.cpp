#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "ford_sync_gen2_family_id.h"
#include "../../socs/imx51/imx51_kpp.h"
#include "../../socs/imx51/imx51_gpio3.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* KPP KPDR[3:0] = HW_REV (nk.exe IOCTL 0x01011070 bit-reverses it); undriven 0
   fails the ipuv3 sub_C0A41460 HW_REV>=10 graphics gate, so IPU/GPU EMEM lands in
   the CSD0 OS pool not reserved CSD1 -> OOM. 0xF = HW_REV 15. */
constexpr uint32_t kHwRevNibble = 0xFu;
/* GPIO3.24 = EOL/factory-jig pin: the IPL (sboot_8FF00000.bin sub_8FF05664) forces
   factory boot mode (no SBOOT USB recovery) when HW_REV>=2 AND GPIO3.PSR[24]==0. A
   shipped unit holds it HIGH (no jig); CERF must too, or HW_REV>=2 traps the IPL in
   EOL mode and a blank NAND never recovery-flashes. */
constexpr uint32_t kEolPin = 24u;

class FordSyncGen2BootStraps : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2;
    }
    void OnReady() override {
        auto& kpp = emu_.Get<Imx51Kpp>();
        for (uint32_t pin = 0; pin < 4u; ++pin)
            kpp.SetInputPin(pin, (kHwRevNibble >> pin) & 1u);
        emu_.Get<Imx51Gpio3>().SetInputPin(kEolPin, true);
    }
};

}  /* namespace */

REGISTER_SERVICE(FordSyncGen2BootStraps);

}
