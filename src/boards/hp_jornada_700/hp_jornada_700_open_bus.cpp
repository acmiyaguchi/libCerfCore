#include "../../peripherals/open_bus_window.h"

#include <cerfcore/core/cerf_emulator.h>
#include "hp_jornada_700_family_id.h"

namespace cerfcore {

namespace {

/* Unpopulated static-bank space between the modem window and the
   CL-CD1284 debug board (SA-1110 Dev Manual ch.2 map). SA-1110 data
   aborts are MMU-generated, so these accesses complete: writes vanish,
   reads float - jlime pushes its post-MMU-off stack here on real HW. */
class HpJornada700OpenBus : public OpenBusWindow {
public:
    using OpenBusWindow::OpenBusWindow;

    uint32_t MmioBase() const override { return 0x08400000u; }
    uint32_t MmioSize() const override { return 0x11C00000u; }

protected:
    std::string_view WindowBoardFamilyId() const override { return BoardFamilyId::HpJornada700; }
    const char* WindowTag()   const override { return "J720OpenBus"; }
};

}  /* namespace */

REGISTER_SERVICE(HpJornada700OpenBus);

}
