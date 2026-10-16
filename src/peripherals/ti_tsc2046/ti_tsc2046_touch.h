#pragma once

#include <cerfcore/core/service.h>
#include "../../socs/omap3530/omap3530_mcspi1.h"

#include <atomic>
#include <cstdint>

namespace cerfcore {

class Tsc2046Touch : public Service, public McspiSlave {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* ADC values must fit in 12 bits (0..4095); higher bits are
       clipped into the SPI response and the driver decodes garbage. */
    void SetState(uint16_t adc_x, uint16_t adc_y, bool pen_down);

    /* PENIRQ is asserted LOW while pen-down. Inverting the polarity
       at the GPIO-wiring site would fire IRQs continuously on pen-up. */
    bool IsPenIrqAsserted() const;

    uint32_t Transfer(uint32_t out_word, uint32_t wl_bits) override;

private:
    std::atomic<uint16_t> adc_x_   {0};
    std::atomic<uint16_t> adc_y_   {0};
    std::atomic<bool>     pen_down_{false};
};

}
