#include "nec_mobilepro_900_series_l1110.h"

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

namespace {

/* CF socket NMC1110 register, PA 0x09000000 (PXA255 socket 0). */
class NecMobilepro900SeriesL1110Cf : public NecMobilepro900SeriesL1110 {
public:
    using NecMobilepro900SeriesL1110::NecMobilepro900SeriesL1110;
    uint32_t MmioBase() const override { return 0x09000000u; }
protected:
    int Socket() const override { return 0; }
};

}  /* namespace */

REGISTER_SERVICE(NecMobilepro900SeriesL1110Cf);

}
