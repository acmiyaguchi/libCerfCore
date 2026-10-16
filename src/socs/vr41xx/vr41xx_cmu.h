#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <cerfcore/peripherals/peripheral_base.h>

namespace cerfcore {

/* CMUCLKMSK D3 MSKKIU and D0 MSKPIU "Supply/mask TClock to KIU / PIU unit, 1: Supply, 0: Mask"
   (VR4102 UM 13.2.1 p290, VR4111 UM 14.2.1 p324, VR4121 UM 14.2.1 p367). */
inline constexpr uint16_t kCmuMskPiu = 1u << 0;
inline constexpr uint16_t kCmuMskKiu = 1u << 3;

class Vr41xxCmu : public Peripheral {
public:
    using Peripheral::Peripheral;

    void RequireTclock(uint16_t mask_bit, const char* operation) const;
    void RegisterClockListener(uint16_t mask_bit, std::function<void(bool supplied_after)> fn);

protected:
    virtual uint16_t ClockMask() const = 0;
    void ClockMaskWriting(uint16_t next) const;

private:
    struct ClockListener {
        uint16_t                  mask_bit;
        std::function<void(bool)> fn;
    };
    std::vector<ClockListener> clock_listeners_;
};

}
