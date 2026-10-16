#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateWriter;
class StateReader;

/* An I2C slave device on a (host-modelled) I2C bus. A controller routes each
   master-driven transaction to the registered slave that answers `slave_addr`.
   Used cross-SoC: OMAP3530 (TWL4030 PMIC) and i.MX51 (MC13892 PMIC). */
class I2cSlave : public Service {
public:
    using Service::Service;
    ~I2cSlave() override = default;

    /* An I2cSlave is a Service, not a Peripheral, so it is absent from the
       hibernation peripheral walk; the owning I2C controller forwards these
       from its own SaveState/RestoreState (hibernation.md). Default no-op. */
    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}

    /* Return true iff this slave answers the given 7-bit I2C
       address. A single slave may answer at multiple addresses
       (the TWL4030 answers at four - one per internal module). */
    virtual bool MatchesAddress(uint8_t slave_addr) const = 0;

    /* Called at the start of every master-driven transaction
       directed at `slave_addr`. Slaves typically reset their
       sub-address-pending flag here so the next master write byte
       is treated as a new sub-address. */
    virtual void TxnStart(uint8_t slave_addr) = 0;

    /* Master is sending us one byte. First byte after TxnStart is
       conventionally the sub-address; subsequent bytes are data
       written into the register file at sub_address++. */
    virtual void TxnWriteByte(uint8_t slave_addr, uint8_t byte) = 0;

    /* Master is reading one byte from us. We return the register
       at the current sub_address and auto-increment. */
    virtual uint8_t TxnReadByte(uint8_t slave_addr) = 0;
};
