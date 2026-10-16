#pragma once

#include <cstdint>

#include <cerfengine/arm/arm_mmu_state.h>

/* ARM DDI 0406C.c Table D15-7 (p. D15-2607): AP=00 by SCTLR.{S,R} - S=0,R=0 no access;
   S=0,R=1 RO all modes; S=1,R=0 PL1 RO; S=1,R=1 reserved. AP=01 PL1 RW; AP=10 PL1 RW +
   user RO; AP=11 full access. */
template <cerfengine::ArmMmuAccess kAccess>
inline bool ApPermits(uint32_t ap, bool is_user_mode,
                      bool sctlr_s, bool sctlr_r) {
    if constexpr (kAccess == cerfengine::ArmMmuAccess::kRead ||
                  kAccess == cerfengine::ArmMmuAccess::kExecute) {
        switch (ap & 3u) {
        case 0u:
            if (!sctlr_s && sctlr_r) return true;
            if (sctlr_s && !sctlr_r) return !is_user_mode;
            return false;
        case 1u:
            return !is_user_mode;
        default:
            return true;
        }
    } else if constexpr (kAccess == cerfengine::ArmMmuAccess::kWrite ||
                         kAccess == cerfengine::ArmMmuAccess::kReadWrite) {
        switch (ap & 3u) {
        case 0u:
            return false;
        case 1u:
        case 2u:
            return !is_user_mode;
        default:
            return true;
        }
    } else {
        return false;
    }
}

template <cerfengine::ArmMmuAccess kAccess>
inline bool ApPermitsV6(uint32_t ap, bool is_user_mode) {
    if (ap == 0u || ap == 4u) return false;
    const bool apx = (ap & 4u) != 0u;
    const uint32_t base = ap & 3u;
    if constexpr (kAccess == cerfengine::ArmMmuAccess::kRead ||
                  kAccess == cerfengine::ArmMmuAccess::kExecute) {
        return is_user_mode ? base >= 2u : true;
    } else if constexpr (kAccess == cerfengine::ArmMmuAccess::kReadWrite ||
                         kAccess == cerfengine::ArmMmuAccess::kWrite) {
        if (apx) return false;
        return is_user_mode ? base == 3u : true;
    } else {
        return false;
    }
}
