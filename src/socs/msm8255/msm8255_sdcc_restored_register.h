#pragma once

#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerf_msm8255_sdcc_detail {

uint32_t ReadRestoredRegister(StateReader& r, const char* name, uint32_t writable,
                              uint32_t base, uint32_t offset);

}  // namespace cerf_msm8255_sdcc_detail
