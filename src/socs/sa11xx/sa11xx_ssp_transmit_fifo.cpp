#include "sa11xx_ssp_transmit_fifo.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_ssp_regs.h"

namespace cerfcore {

using namespace sa11xx_ssp;

/* SA-1110 Developer's Manual §11.12.12.4 (page 11-164): TFS "is set whenever the transmit FIFO has
   four or fewer entries of valid data"; §11.12.7.3 (page 11-155): "The width of each entry within
   the FIFOs is 16 bits". */
Sa11xxSspTransmitFifo::Sa11xxSspTransmitFifo(CerfEmulator& emu)
    : Sa11xxDmaTransmitFifo(emu, Geometry{kTxDepth, kBurst, kBurst, 0xFFFFu}) {}

REGISTER_SERVICE(Sa11xxSspTransmitFifo);

}
