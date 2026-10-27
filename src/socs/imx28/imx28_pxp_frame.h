#pragma once

#include <cstdint>
#include <vector>

namespace cerfcore {

class CerfEmulator;

/* MCIMX28RM Rev. 2 sections 34.2, 34.2.2 and 34.2.9: one PXP frame from S0 into OUTBUF. */
class Imx28PxpFrame {
public:
    explicit Imx28PxpFrame(CerfEmulator& emu) : emu_(emu) {}

    void Render(const uint32_t* regs);

private:
    [[noreturn]] void Die(const char* what, uint32_t value) const;
    void FillPre(const uint32_t* regs, uint32_t pre_w, uint32_t pre_h);
    void FillPreYuv420(const uint32_t* regs, uint32_t pre_w, uint32_t pre_h);
    void Rotate(uint32_t rotate, uint32_t out_w, uint32_t out_h, uint32_t pre_w, uint32_t pre_h);

    CerfEmulator& emu_;
    std::vector<uint16_t> pre_;
    std::vector<uint16_t> out_;
    std::vector<uint8_t>  line_;
    std::vector<uint8_t>  plane_y_;
    std::vector<uint8_t>  plane_u_;
    std::vector<uint8_t>  plane_v_;
};

}
