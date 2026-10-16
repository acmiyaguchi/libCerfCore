#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class FreescaleSdmaChannelHost;
class FreescaleSsiCore;

class FreescaleSsiSdmaWiring : public Service {
public:
    using Service::Service;

    virtual FreescaleSdmaChannelHost& Sdma() = 0;
    virtual FreescaleSsiCore&         Core(uint32_t ssi) = 0;
    virtual uint32_t                  SsiCount() const = 0;
    virtual bool     TxEventFifo(int event, uint32_t& ssi, uint32_t& fifo) const = 0;
    virtual bool     RxEventFifo(int event, uint32_t& ssi, uint32_t& fifo) const = 0;
    virtual uint32_t RxSampleBytes(uint32_t ssi) = 0;
};

}
