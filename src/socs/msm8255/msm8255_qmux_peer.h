#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Msm8255QmuxPeer : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint32_t Answer(uint32_t in_pa, uint32_t in_bytes, uint32_t out_pa,
                    uint32_t out_cap);
};

}
