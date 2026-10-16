#include "sa11xx_ssp_receive_fifo.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_ssp_regs.h"

namespace cerfcore {

using namespace sa11xx_ssp;

/* SA-1110 Developer's Manual §11.12.12.5 (page 11-165): RFS "is set whenever the receive FIFO has
   four or more entries of valid data". */
Sa11xxSspReceiveFifo::Sa11xxSspReceiveFifo(CerfEmulator& emu)
    : Sa11xxDmaReceiveFifo(emu, Geometry{kRxDepth, kBurst, kBurst}) {}

REGISTER_SERVICE(Sa11xxSspReceiveFifo);

}
