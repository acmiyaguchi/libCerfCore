#pragma once

#include <cstdint>

namespace cerfcore {

class CerfEmulator;

/* MCIMX28RM Rev. 2 section 31.2.6.5 device data structures: dQH list and dTD execution. */
class Imx28UsbEndpoints {
public:
    struct Transfer {
        uint32_t moved;
        bool     ioc;
        bool     short_packet;
        bool     more;
    };

    Imx28UsbEndpoints(CerfEmulator& emu, uint32_t base) : emu_(emu), base_(base) {}

    void     Fetch(uint32_t list, uint32_t bit, uint32_t value);
    void     WriteSetup(uint32_t list, const uint8_t* setup);
    bool     InterruptOnSetup(uint32_t list);
    Transfer Execute(uint32_t list, uint32_t bit, uint8_t* data, uint32_t length);

private:
    uint32_t Read(uint32_t addr);
    void     Write(uint32_t addr, uint32_t value);
    void     Copy(uint32_t qh, uint8_t* data, uint32_t length, bool tx);
    [[noreturn]] void Die(const char* what, uint32_t addr, uint32_t value) const;

    CerfEmulator& emu_;
    uint32_t      base_;
};

}
