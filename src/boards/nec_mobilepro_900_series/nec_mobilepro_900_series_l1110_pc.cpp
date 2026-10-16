#include "nec_mobilepro_900_series_l1110.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

/* PC Card socket NMC1110 register, PA 0x0A000000 (PXA255 socket 1). */
class NecMobilepro900SeriesL1110Pc : public NecMobilepro900SeriesL1110 {
public:
    using NecMobilepro900SeriesL1110::NecMobilepro900SeriesL1110;
    uint32_t MmioBase() const override { return 0x0A000000u; }
protected:
    int Socket() const override { return 1; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilepro900SeriesL1110Pc);

}
