#include "../../socs/vr41xx/vr41xx_serial_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

namespace cerfcore {

namespace {

/* Carrier reaches the guest on GIU pin 15, asserted low: casio_cassiopeia_e55 serial.dll
   sub_14A0E90 @0x14A0EEC sets MS_RLSD_ON when sub_14A2B24 returns 0, and sub_14A2AE8
   @0x14A2B08 latches that word from GIUPIODL & 0x8000. CTS/DSR/RI come from the SIU's own
   MSR at +6 (sub_14A0E90 @0x14A0E90). */
class CasioTomcatSerialWiring : public Vr41xxSerialWiring {
public:
    using Vr41xxSerialWiring::Vr41xxSerialWiring;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    std::optional<Vr41xxSerialModem> ForSiu() const override {
        Vr41xxSerialModem m;
        m.label       = "COM1";
        m.dcd_giu_pin = 15;
        return m;
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioTomcatSerialWiring, Vr41xxSerialWiring);

}
