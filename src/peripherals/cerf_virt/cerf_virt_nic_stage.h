#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class CerfVirtNicStage : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t BasePa() const;

    uint8_t* TxSlot(uint32_t index);
    uint8_t* RxSlot(uint32_t index);

private:
    uint8_t* base_ = nullptr;
};

}
