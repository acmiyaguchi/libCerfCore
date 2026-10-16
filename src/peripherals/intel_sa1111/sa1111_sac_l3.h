#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateReader;
class StateWriter;

class Sa1111SacL3 : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint32_t Address() const { return car_; }
    void     WriteAddress(uint32_t value);
    void     WriteData(uint32_t value, uint32_t sacr1);
    uint32_t ReadData() const { return rx_; }
    uint32_t StatusBits() const;
    void     ClearStatus(uint32_t sascr);
    bool     DataSent() const { return sent_; }
    bool     DataRead() const { return received_; }
    void     OnClockChange(bool running);

    void Reset();

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    void Settle();

    uint32_t car_       = 0;
    bool     data_      = false;
    bool     sent_      = false;
    bool     read_      = false;
    bool     received_  = false;
    bool     running_   = false;
    uint8_t  rx_        = 0;
};
