#pragma once

#include "../freescale_sdma_impl.h"
#include "imx31_id.h"

namespace cerfcore {

/* MCIMX31RM Table 40-10: SDMA at 0x53FD_4000. Named (not anonymous) so
   Imx31AudioPlayer can register a channel sink on it. */
class Imx31Sdma
    : public cerf_freescale_sdma_detail::FreescaleSdmaBase<0x53FD4000u,
                                                           SocId::Imx31> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

protected:
    void AssertIrqLine()   override;
    void DeassertIrqLine() override;

    uint32_t ChnenblBase()  const override { return 0x80u; }
    uint32_t ChnenblCount() const override { return 32u; }

    bool UnmappedRingIdles() const override { return false; }

    bool ReadExtra(uint32_t off, uint32_t& out) override;
};

}
