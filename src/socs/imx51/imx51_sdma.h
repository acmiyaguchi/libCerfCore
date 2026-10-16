#pragma once

#include "../freescale_sdma_impl.h"

#include "../irq_controller.h"
#include "imx51_uart1.h"
#include "imx51_uart3.h"
#include <cerfcore/core/cerf_emulator.h>
#include "imx51_id.h"

/* SDMA AP interrupt = TZIC source 6 (MCIMX51RM Table 3-2, ARM Domain Interrupt
   Summary). */
constexpr uint32_t kTzicSourceSdma = 6u;

class Imx51Sdma : public cerf_freescale_sdma_detail::FreescaleSdmaBase<0x83FB0000u,
                                                                      SocId::Imx51> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

    void OnReady() override {
        FreescaleSdmaBase::OnReady();
        BindUarts();
    }

protected:
    void AssertIrqLine()   override { emu_.Get<IrqController>().AssertIrq(kTzicSourceSdma); }
    void DeassertIrqLine() override { emu_.Get<IrqController>().DeAssertIrq(kTzicSourceSdma); }

    uint32_t ChnenblBase()  const override { return kOffChnenblBase; }
    uint32_t ChnenblCount() const override { return kChnenblCount; }

    /* cspddk runs every i.MX51 SDMA channel with extended 12-byte BDs: its
       per-event channel setup (sub_C09C3FAC) sets the EXTD flag for all events,
       and DDKSdmaGet/SetBufDesc index BDs at 12*idx. */
    uint32_t BdStride(uint32_t) const override { return 12u; }

    bool UnmappedRingIdles() const override { return true; }

    bool ReadExtra(uint32_t off, uint32_t& out) override {
        if (off == kOffEvtMirror) { out = 0; return true; }   /* no DMA-request events */
        return false;
    }

private:
    static constexpr uint32_t kOffEvtMirror   = 0x60u;
    static constexpr uint32_t kOffChnenblBase = 0x200u;
    static constexpr uint32_t kChnenblCount   = 48u;

    /* UART1 (COM1) and UART3 move serial data over SDMA. The CHNENBL index is the
       MCIMX51RM Table 3-3 event number: UART1 RX=18/TX=19, UART3 RX=43/TX=44. */
    void BindUarts() {
        emu_.Get<Imx51Uart1>().BindSdma(this, 19u, 18u);
        emu_.Get<Imx51Uart3>().BindSdma(this, 44u, 43u);
    }
};
