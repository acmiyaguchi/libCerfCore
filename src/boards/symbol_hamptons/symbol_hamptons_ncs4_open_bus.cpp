#include "../../peripherals/open_bus_window.h"

#include <cerfcore/core/cerf_emulator.h>
#include "symbol_hamptons_family_id.h"

namespace cerfcore {

namespace {

/* Intel PXA27x Developer's Manual 280000-001 Figure 6-11 (page 6-71):
   "0x1000_0000" "Static nCS<4> (64 MB)" in both map options.

   symbol_mk500 SMSC9211.dll (ImageBase 0x10000000) RVA 0x2C18 "LDR r3,[r1,#0x64]" /
   0x2C1C "CMP r3,r2" against the RVA 0x2D38 literal 0x87654321 / 0x2C20
   "BNE 0x2D2C" / 0x2D2C "MOV r0,#0" / 0x2D34 "BX lr". */
class SymbolHamptonsNCs4OpenBus : public OpenBusWindow {
public:
    using OpenBusWindow::OpenBusWindow;

    uint32_t MmioBase() const override { return 0x10000000u; }
    uint32_t MmioSize() const override { return 0x04000000u; }

protected:
    std::string_view WindowBoardFamilyId() const override { return BoardFamilyId::SymbolHamptons; }
    const char* WindowTag()   const override { return "MK500 nCS4 OpenBus"; }
};

}  /* namespace */

REGISTER_SERVICE(SymbolHamptonsNCs4OpenBus);

}
