#pragma once

#include "imx28_apb_dma.h"

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 6 (APBH-Bridge-DMA) at 0x8000_4000. */
class Imx28Apbh : public Imx28ApbDma {
public:
    using Imx28ApbDma::Imx28ApbDma;

    bool ShouldRegister() override;
    void OnReady() override { Init(); }

    uint32_t MmioBase() const override { return 0x80004000u; }

protected:
    const Imx28ApbDmaSpec& Spec() const override;
};

}
