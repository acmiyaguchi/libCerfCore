#pragma once

#include "../../socs/pr31x00/pr31x00_spi_slave.h"

#include <cstdint>
#include <deque>
#include <mutex>

class PhilipsVelo1KeyboardEc : public Pr31x00SpiSlave {
public:
    using Pr31x00SpiSlave::Pr31x00SpiSlave;
    bool ShouldRegister() override;
    void OnReady() override;

    void InjectKey(uint8_t scancode, bool key_up);

    bool    SpiRxHasByte() override;
    uint8_t SpiRxReadByte() override;
    void    SpiTxByte(uint8_t byte) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void StageEnableLocked();     /* caller holds mtx_ */
    void OnSpiRcvIntEnabled();
    void Unhandshake();

    std::mutex mtx_;
    std::deque<uint8_t> tx_;
    bool enabled_ = false;
};
