#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class CerfVirtDmaArena : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t BasePa() const;
    uint32_t SizeBytes() const;

    uint8_t* At(uint32_t offset, uint32_t bytes);

    void Claim(uint32_t pid);

private:
    uint32_t PartitionBase(uint32_t index) const;
    void     ZeroOwners();

    uint8_t* base_ = nullptr;
};
