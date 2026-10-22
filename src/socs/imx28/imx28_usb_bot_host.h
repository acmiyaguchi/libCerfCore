#pragma once

#include <cstdint>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class Imx28Usbctrl;
class StateReader;
class StateWriter;

/* USB Mass Storage Class Bulk-Only Transport 1.0 sections 5.1-5.2 host side: CBW, data, CSW. */
class Imx28UsbBotHost {
public:
    explicit Imx28UsbBotHost(CerfEmulator& emu) : emu_(emu) {}

    void Start(uint32_t in_bit, uint32_t out_bit);
    bool Step(Imx28Usbctrl& port);
    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    enum Phase : uint32_t { kOff, kCbw, kData, kCsw, kDone };

    void SendCbw(Imx28Usbctrl& port);
    void ReadData(Imx28Usbctrl& port);
    void ReadCsw(Imx28Usbctrl& port);
    void Finish();
    [[noreturn]] void Die(const char* what, uint32_t a, uint32_t b) const;

    CerfEmulator&        emu_;
    uint32_t             phase_   = kOff;
    uint32_t             command_ = 0;
    uint32_t             in_bit_  = 0;
    uint32_t             out_bit_ = 0;
    uint32_t             tag_     = 0;
    uint32_t             length_  = 0;
    uint32_t             block_   = 0;
    std::vector<uint8_t> data_;
};

}
