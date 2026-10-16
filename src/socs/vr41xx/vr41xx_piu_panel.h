#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <optional>

namespace cerfcore {

/* The resistive panel wired to the VR41xx PIU's A/D ports. PIUCMDREG ADCMD(3:0) selects
   which port a command scan converts - 0000 TPX0, 0001 TPX1, 0010 TPY0, 0011 TPY1,
   0100 ADIN0, 0101 ADIN1, 0110 ADIN2, 0111 AUDIOIN (VR4121 UM 20.3.5 (2/2), VR4102 UM
   19.3.5 (2/2)) - and what each port carries is decided by the board's wiring. */
class Vr41xxPiuPanel : public Service {
public:
    using Service::Service;

    /* std::nullopt leaves PIUABnREG's D15 VALID clear, which the A/D buffer defines as
       "0: Invalid" (VR4121 UM 20.3.10, VR4102 UM 19.3.10). */
    virtual std::optional<uint16_t> ConvertCommandPort(uint16_t adcmd,
                                                       uint16_t pos_x,
                                                       uint16_t pos_y) = 0;

    /* PIUPBn4REG carries Z (touch pressure) (VR4121 UM Table 20-4, VR4102 UM Table 19-4);
       std::nullopt leaves its D15 VALID clear ("0: Invalid", VR4121 UM 20.3.9). */
    virtual std::optional<uint16_t> PressureSample() = 0;

    /* One ADPortScan A/D conversion of the port whose ADCMD index is `port` (0-3 TPX0/TPX1/
       TPY0/TPY1, 4-6 ADIN(2:0), 7 AUDIOIN; VR4121 UM Table 20-5, VR4102 UM Table 19-5).
       std::nullopt leaves PIUABnREG's D15 VALID clear (VR4121 UM 20.3.10, VR4102 UM 19.3.10). */
    virtual std::optional<uint16_t> AdPortScanSample(uint16_t port) = 0;

    virtual uint16_t ScreenRawXMax() const { return 1023u; }
};

}
