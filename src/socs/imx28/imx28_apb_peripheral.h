#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 section 39.3: SCT aliases read back 0. */
class Imx28ApbPeripheral : public Peripheral {
public:
    using Peripheral::Peripheral;

    uint8_t  ReadByte (uint32_t addr) override;
    uint16_t ReadHalf (uint32_t addr) override;
    void     WriteByte(uint32_t addr, uint8_t  value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

protected:
    /* Section 5.4.3 CTRL: SFTRST [31], CLKGATE [30]. */
    static constexpr uint32_t kCtrlSftrst  = 1u << 31;
    static constexpr uint32_t kCtrlClkgate = 1u << 30;

    static bool Gated(uint32_t ctrl) { return (ctrl & (kCtrlSftrst | kCtrlClkgate)) != 0u; }

    virtual uint32_t ReadLanes(uint32_t word_addr, uint32_t lanes);
    virtual uint32_t PeekLanes(uint32_t word_addr, uint32_t lanes);
    virtual bool     HasSct() const { return true; }

    /* MCIMX28RM Rev. 2 sections 5.3 and 39.5.10. */
    bool IsSoftReset(uint32_t old_ctrl, uint32_t next_ctrl) const;

    void RequireOperating(const char* op, uint32_t ctrl, uint32_t addr, uint32_t value) const;

private:
    uint32_t MergeNarrow(uint32_t addr, uint32_t shift, uint32_t mask, uint32_t value);
};

}
