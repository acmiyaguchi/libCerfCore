#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateWriter;
class StateReader;

/* A device on the PR31x00 SPI bus (TMPR3911/3912 ch.14). The SPI module serves a
   RXDATA read from the attached slave; the slave raises SPIRCVINT through the INTC
   while it has a character ready, which is how serial.dll's SPI IST (SYSINTR on
   SPIRCVINT) knows to read RXDATA. */
class Pr31x00SpiSlave : public Service {
public:
    using Service::Service;

    virtual bool    SpiRxHasByte() = 0;
    virtual uint8_t SpiRxReadByte() = 0;

    /* A TXDATA write clocks a byte from the SPI module to the slave (§14.3.2). */
    virtual void    SpiTxByte(uint8_t byte) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};
