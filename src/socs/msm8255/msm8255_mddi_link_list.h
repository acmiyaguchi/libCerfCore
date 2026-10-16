#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class Msm8255MddiLinkListHost {
public:
    virtual ~Msm8255MddiLinkListHost() = default;

    virtual void QueueRegisterReadResponse(uint32_t address,
                                           uint32_t value) = 0;
};

class Msm8255MddiLinkList : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Execute(uint32_t head_pa, Msm8255MddiLinkListHost& host);

private:
    uint32_t ReadUnalignedWord(uint32_t pa);
    void     ExecuteItem(uint32_t item_pa, Msm8255MddiLinkListHost& host);
    void     RequireHeaderBytes(uint32_t item_pa, uint32_t have, uint32_t want);
    void     RequireFraming(uint32_t item_pa, uint32_t packet_pa,
                            uint32_t header_bytes, uint32_t data_bytes);
    void     ExecuteRegisterAccess(uint32_t item_pa, uint32_t packet_pa,
                                   uint32_t data_bytes,
                                   Msm8255MddiLinkListHost& host);
    void     ExecuteVideoStream(uint32_t item_pa, uint32_t packet_pa,
                                uint32_t data_bytes);
};
