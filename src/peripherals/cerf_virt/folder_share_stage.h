#pragma once

#include <cerfcore/core/service.h>
#include "cerf_virt_folder_share_regs.h"

#include <cstdint>

namespace cerfcore {

class FolderShareStage : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t BasePa() const;

    CerfVirt::ServerPB* Pb();
    uint8_t*            IoBuf();

private:
    uint8_t* base_ = nullptr;
};

}
