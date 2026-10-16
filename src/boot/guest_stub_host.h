#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

struct GuestStubHostSpan {
    uint32_t va    = 0;
    uint32_t pa    = 0;
    uint32_t avail = 0;
    bool     squat = false;
};

class GuestStubHost : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    GuestStubHostSpan Pick(const char* victim_name, uint32_t o32_pa,
                           uint16_t objcnt, uint32_t foot_bytes, bool in_place);

private:
    bool LargestVictimSection(uint32_t o32_pa, uint16_t objcnt,
                              uint32_t& out_va, uint32_t& out_size);
    const char* SquatReject(uint32_t sq_va, uint32_t sq_size,
                            uint32_t foot_bytes, bool in_place, uint32_t& out_pa);

    uint32_t band_next_ = 0;
};

}
