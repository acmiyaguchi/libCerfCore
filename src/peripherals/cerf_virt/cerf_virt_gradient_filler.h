#pragma once

#include <cerfcore/peripherals/cerf_virt/cerf_virt_grad_descriptor.h>
#include "cerf_virt_blt_surface.h"
#include <cerfcore/core/service.h>

namespace CerfVirt {

class CerfVirtGradientFiller : public Service, public BltSurfaceAccess {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    bool Execute(const CerfGradDescriptor& g);
};

}
