#include "../../socs/vr41xx/vr41xx_serial_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

namespace {

/* Carrier reaches the guest on GIU pin 5, asserted low: serial.dll HWGetModemStatus
   (sub_15810C4) sets MS_RLSD_ON when (*(WORD*)0x0B000104 & 0x20) == 0, and HWInit arms
   that pin's GIU interrupt. The SIU's fixed DCD# input PIOD[15] (UM 18.2.3) is unused on
   this board. */
class NecMobilePro700SerialWiring : public Vr41xxSerialWiring {
public:
    using Vr41xxSerialWiring::Vr41xxSerialWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    std::optional<Vr41xxSerialModem> ForSiu() const override {
        Vr41xxSerialModem m;
        m.label       = "COM1";
        m.dcd_giu_pin = 5;
        return m;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecMobilePro700SerialWiring, Vr41xxSerialWiring);
