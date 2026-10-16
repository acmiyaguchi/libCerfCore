#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

/* Board-side consumer of the SA-1111 GPIO port A output pins (the
   PCMCIA/CF power controller hangs off GPIO_A per the PCCR note in
   Developer's Manual §12.6.2). Optional - boards without a consumer
   simply have no impl registered. */
class Sa1111GpioPortASink : public Service {
public:
    using Service::Service;
    ~Sa1111GpioPortASink() override = default;

    /* Driven output levels: DWR & ~DDR. */
    virtual void OnPortAOutputs(uint8_t levels) = 0;
};
