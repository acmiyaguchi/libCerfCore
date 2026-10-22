#pragma once

#include "../i2c_slave.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 27.2.2: master-mode bus protocol seen by the I2C target. */
class Imx28I2cMaster {
public:
    struct Command {
        bool     start;
        bool     stop;
        uint32_t count;
    };

    Imx28I2cMaster(CerfEmulator& emu, uint32_t mmio_base) : emu_(emu), mmio_base_(mmio_base) {}

    void SetSlave(I2cSlave* slave) { slave_ = slave; }
    void Reset();

    bool Transmit(const Command& c, const uint8_t* data);
    void Receive(const Command& c, uint8_t* out);

    bool BusBusy() const { return busy_; }

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    [[noreturn]] void Die(const char* what, uint32_t value) const;

    CerfEmulator& emu_;
    const uint32_t mmio_base_;
    I2cSlave*  slave_   = nullptr;
    bool       busy_    = false;
    bool       reading_ = false;
    uint8_t    addr_    = 0u;
};

}
