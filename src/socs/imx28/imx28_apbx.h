#pragma once

#include "imx28_apb_dma.h"

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 7 (APBX-Bridge-DMA) at 0x8002_4000. */
class Imx28Apbx : public Imx28ApbDma {
public:
    using Imx28ApbDma::Imx28ApbDma;

    bool ShouldRegister() override;
    void OnReady() override { Init(); }

    uint32_t MmioBase() const override { return 0x80024000u; }

protected:
    const Imx28ApbDmaSpec& Spec() const override;
};

}
