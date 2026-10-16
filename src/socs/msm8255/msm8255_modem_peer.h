#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class IrqController;

class Msm8255ModemPeer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RingDoorbell(uint32_t mask);

private:
    void SeedProcCommReady();
    void PublishModemState();
    void NotifySmd();
    void ServiceSmdChannel(uint32_t cid, uint32_t rec, uint32_t item);
    void ConsumeAppsSmdFlags(uint32_t cid, uint32_t rec, uint32_t apps_half_pa);
    bool ChannelNameIs(uint32_t rec, const char* name, uint32_t bytes);
    bool ChannelIsQmuxControl(uint32_t rec);
    [[noreturn]] void HaltUnroutedSmdChannel(uint32_t cid, uint32_t rec);
    void ServiceSmdData(uint32_t cid, uint32_t rec, uint32_t apps_half_pa);
    uint32_t AnswerQmuxControlPacket(uint32_t in_pa, uint32_t in_avail,
                                     uint32_t ring_bytes, uint32_t out_pa,
                                     uint32_t out_cap, uint32_t& consumed);
    void OpenModemSmdHalf(uint32_t modem_half_pa);
    void RunProcComm();

    IrqController* irq_ = nullptr;
};
