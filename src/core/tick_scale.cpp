#include "tick_scale.h"

#include <cerfcore/core/log.h>

namespace cerfcore {

void ExactScale::Set(const WideUint<2>& num, const WideUint<2>& den) {
    num_          = num;
    den_          = den;
    narrow_floor_ = NarrowFloorFits(num, den);
    narrow_ceil_  = NarrowCeilFits(num, den);
}

uint64_t ExactScale::WideDivide(uint64_t value, WideUint<2>* rem) const {
    return DivideWide(MultiplyWide(WideUint<1>::Of(value), num_), den_, rem);
}

uint64_t ExactScale::WideCeil(uint64_t value) const {
    WideUint<2>    rem;
    const uint64_t q = WideDivide(value, &rem);
    return q + (rem.IsZero() ? 0u : 1u);
}

WideUint<2> ExactScale::Remainder(uint64_t value) const {
    if (narrow_floor_) return WideUint<2>::Of(RemainderU64(value));
    WideUint<2> rem;
    WideDivide(value, &rem);
    return rem;
}

uint64_t ExactScale::FloorRemU64(uint64_t value, uint64_t& rem) const {
    if (!narrow_floor_) return WideFloorRemU64(value, rem);
    return ScaleFloorRemU64(value, num_.w[0], den_.w[0], rem);
}

uint64_t ExactScale::WideFloorRemU64(uint64_t value, uint64_t& rem) const {
    RequireDenU64();
    WideUint<2>    r;
    const uint64_t q = WideDivide(value, &r);
    rem              = r.w[0];
    return q;
}

uint64_t ExactScale::WideRemainderU64(uint64_t value) const {
    RequireDenU64();
    return Remainder(value).w[0];
}

uint64_t ExactScale::RequireDenU64() const {
    if (!den_.FitsU64()) {
        LOG(Caution, "ExactScale: a 64-bit read of a %d-bit denominator\n", den_.BitLength());
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    return den_.w[0];
}

}
