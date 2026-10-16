#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

/* Board-side consumer of iPAQ EGPIO latch changes (the option-pack
   power enable lives there). Optional - TryGet'd by the latch. */
class CompaqIpaqH3000EgpioSink : public Service {
public:
    using Service::Service;
    ~CompaqIpaqH3000EgpioSink() override = default;

    virtual void OnEgpioChanged(uint32_t latched) = 0;
};
