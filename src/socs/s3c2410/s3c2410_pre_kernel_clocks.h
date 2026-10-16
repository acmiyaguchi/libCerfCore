#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class S3C2410PreKernelClocks : public Service {
public:
    using Service::Service;

    virtual uint32_t MpllCon() const = 0;
    virtual uint32_t ClkDivn() const = 0;
};

}
