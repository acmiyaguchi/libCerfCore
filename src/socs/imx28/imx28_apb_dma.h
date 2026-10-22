#pragma once

#include <cerfcore/state/state_stream.h>
#include "imx28_apb_dma_channel.h"
#include "imx28_apb_dma_device.h"
#include "imx28_apb_dma_spec.h"
#include "imx28_apb_peripheral.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <memory>

namespace cerfcore {

class Imx28Icoll;

/* MCIMX28RM Rev. 2 chapters 6 and 7: AHB-to-APBH and AHB-to-APBX bridges with DMA. */
class Imx28ApbDma : public Imx28ApbPeripheral, private Imx28ApbDmaArbiter {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void AttachDevice(uint32_t channel, Imx28ApbDmaDevice* device);
    void NotifyEndCommand(uint32_t channel);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    virtual const Imx28ApbDmaSpec& Spec() const = 0;

    void Init();

    uint32_t ReadLanes(uint32_t word_addr, uint32_t lanes) override;

private:
    bool     Operating() const { return !Gated(ctrl0_); }
    bool     ChannelGranted(uint32_t c) const override;
    void     CommandComplete(uint32_t c) override;
    void     DeviceTerminated(uint32_t c) override;
    Imx28ApbDmaChannel& Channel(uint32_t c);
    void     LoadResetValues();
    void     DriveLines(bool force);
    void     RunPending();
    void     WriteCtrl0(Imx28SctOp op, uint32_t value);
    void     WriteChannelCtrl(Imx28SctOp op, uint32_t value);
    void     WriteBurstSize(uint32_t value);
    void     WriteDebug(uint32_t value);
    void     WriteChannelRegister(uint32_t c, uint32_t reg, uint32_t addr, uint32_t value);
    uint32_t ReadChannelRegister(uint32_t c, uint32_t reg, uint32_t addr);

    std::unique_ptr<Imx28ApbDmaChannel> channels_[kImx28ApbDmaChannels];
    Imx28Icoll* icoll_ = nullptr;

    uint32_t ctrl0_  = 0u;
    uint32_t ctrl1_  = 0u;
    uint32_t ctrl2_  = 0u;
    uint32_t freeze_ = 0u;
    uint32_t burst_  = 0u;
    uint32_t debug_  = 0u;
    uint32_t line_[4] = {};
};

}
