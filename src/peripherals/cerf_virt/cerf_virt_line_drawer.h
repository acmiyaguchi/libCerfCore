#pragma once

#include <cerfcore/peripherals/cerf_virt/cerf_virt_line_descriptor.h>
#include "cerf_virt_blt_surface.h"
#include <cerfcore/core/service.h>

namespace cerfcore {

namespace CerfVirt {

class CerfVirtLineDrawer : public Service, public BltSurfaceAccess {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    bool Execute(const CerfLineDescriptor& l);
};

}

}
