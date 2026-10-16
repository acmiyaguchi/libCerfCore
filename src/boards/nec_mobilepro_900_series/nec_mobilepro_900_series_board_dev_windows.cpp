#include "nec_mobilepro_900_series_board_window.h"

#include <cerfcore/core/cerf_emulator.h>

namespace {

/* Static CS2 board device (PA 0x08000000, OAT VA 0x88000000) - trueffs.dll probes
   it for a DiskOnChip flash; returns-0 makes the probe find no signature. */
class NecMobilepro900SeriesCs2Window : public NecMobilepro900SeriesBoardWindow {
public:
    using NecMobilepro900SeriesBoardWindow::NecMobilepro900SeriesBoardWindow;
    uint32_t MmioBase() const override { return 0x08000000u; }
protected:
    const char* WindowName() const override { return "cs2-dev@0x08000000"; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilepro900SeriesCs2Window);
