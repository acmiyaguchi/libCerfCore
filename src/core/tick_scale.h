#pragma once

#include "wide_uint.h"

#include <cstdint>

namespace cerfcore {

inline uint64_t ScaleU64(uint64_t value, uint64_t num, uint64_t den) {
    return (value / den) * num + ((value % den) * num) / den;
}

inline uint64_t ScaleU64Ceil(uint64_t value, uint64_t num, uint64_t den) {
    return (value / den) * num + ((value % den) * num + den - 1u) / den;
}

inline uint64_t ScaleRemainderU64(uint64_t value, uint64_t num, uint64_t den) {
    return value % den * num % den;
}

inline uint64_t ScaleFloorRemU64(uint64_t value, uint64_t num, uint64_t den, uint64_t& rem) {
    const uint64_t r = ScaleRemainderU64(value, num, den);
    const uint64_t q = ScaleU64(value, num, den);
    rem              = r;
    return q;
}

inline uint64_t MulDivU64(uint64_t a, uint64_t b, uint64_t den, uint64_t& rem) {
    WideUint<1>    r;
    const uint64_t q =
        DivideWide(MultiplyWide(WideUint<1>::Of(a), WideUint<1>::Of(b)), WideUint<1>::Of(den), &r);
    rem = r.w[0];
    return q;
}

inline uint64_t MulDivU64(uint64_t a, uint64_t b, uint64_t den) {
    uint64_t rem = 0u;
    return MulDivU64(a, b, den, rem);
}

inline bool ScaleFitsU64(uint64_t num, uint64_t den) {
    return (den - 1u) <= UINT64_MAX / num;
}

inline bool ScaleCeilFitsU64(uint64_t num, uint64_t den) {
    return (den - 1u) <= (UINT64_MAX - (den - 1u)) / num;
}

inline bool NarrowFloorFits(const WideUint<2>& num, const WideUint<2>& den) {
    return num.FitsU64() && den.FitsU64() && ScaleFitsU64(num.w[0], den.w[0]);
}

inline bool NarrowCeilFits(const WideUint<2>& num, const WideUint<2>& den) {
    return num.FitsU64() && den.FitsU64() && ScaleCeilFitsU64(num.w[0], den.w[0]);
}

class ExactScale {
public:
    void Set(const WideUint<2>& num, const WideUint<2>& den);

    uint64_t Floor(uint64_t value) const {
        return narrow_floor_ ? ScaleU64(value, num_.w[0], den_.w[0]) : WideDivide(value, nullptr);
    }

    uint64_t Ceil(uint64_t value) const {
        return narrow_ceil_ ? ScaleU64Ceil(value, num_.w[0], den_.w[0]) : WideCeil(value);
    }

    uint64_t RemainderU64(uint64_t value) const {
        return narrow_floor_ ? ScaleRemainderU64(value, num_.w[0], den_.w[0])
                             : WideRemainderU64(value);
    }

    uint64_t FloorRemU64(uint64_t value, uint64_t& rem) const;

    uint64_t DenU64() const { return narrow_floor_ ? den_.w[0] : RequireDenU64(); }

    WideUint<2> Remainder(uint64_t value) const;

    const WideUint<2>& Num() const { return num_; }
    const WideUint<2>& Den() const { return den_; }

private:
    uint64_t WideDivide(uint64_t value, WideUint<2>* rem) const;
    uint64_t WideFloorRemU64(uint64_t value, uint64_t& rem) const;
    uint64_t WideCeil(uint64_t value) const;
    uint64_t WideRemainderU64(uint64_t value) const;
    uint64_t RequireDenU64() const;

    WideUint<2> num_          = WideUint<2>::Of(1u);
    WideUint<2> den_          = WideUint<2>::Of(1u);
    bool        narrow_floor_ = true;
    bool        narrow_ceil_  = true;
};

}
