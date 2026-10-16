#include "imx51_sdma.h"

/* i.MX51 divergent registers (MCIMX51RM Table 52-9): EVT_MIRROR @0x60 (read-only
   DMA-request mirror), CHNENBL @0x200, 48 events. Base is 0x83FB0000 per
   MCIMX51RM Table 2-1 and the guest's own access; the Ch 52 register-table base
   0x83FD4000 is a doc error inherited from the i.MX31/i.MX35 SDMA. */

REGISTER_SERVICE(Imx51Sdma);
