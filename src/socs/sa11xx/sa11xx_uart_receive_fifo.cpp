#include "sa11xx_uart_receive_fifo.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_uart_regs.h"

/* §11.11.7.2 (printed 11-120): "When the RFS bit is set, a DMA service request is made"; §11.11.7.6
   (printed 11-121): "DMA requests to empty the receive FIFO are disabled until EIF is cleared". */
Sa11xxUartReceiveFifo::Sa11xxUartReceiveFifo(CerfEmulator& emu)
    : Sa11xxDmaReceiveFifo(emu, Geometry{sa11xx_uart::kRxDepth, sa11xx_uart::kRxRequest,
                                         sa11xx_uart::kRxBurst, sa11xx_uart::kRxErrorWindow,
                                         sa11xx_uart::kRxErrorTags, sa11xx_uart::kRxTagOverrun}) {}
