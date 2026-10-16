#include "sa11xx_uart_transmit_fifo.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_uart_regs.h"

Sa11xxUartTransmitFifo::Sa11xxUartTransmitFifo(CerfEmulator& emu)
    : Sa11xxDmaTransmitFifo(emu, Geometry{sa11xx_uart::kTxDepth, sa11xx_uart::kTxRequest,
                                          sa11xx_uart::kTxBurst, 0xFFu}) {}
