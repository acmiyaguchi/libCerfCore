#pragma once

#include <cstdint>
#include <type_traits>

#include <cerfengine/wide_multiply.h>

template <int N>
struct WideUint {
    uint64_t w[N] = {};

    static WideUint Of(uint64_t v) {
        WideUint r;
        r.w[0] = v;
        return r;
    }

    template <int M>
    WideUint<M> Resized() const {
        WideUint<M> r;
        for (int i = 0; i < M && i < N; ++i) r.w[i] = w[i];
        return r;
    }

    bool FitsU64() const {
        for (int i = 1; i < N; ++i) {
            if (w[i] != 0u) return false;
        }
        return true;
    }

    bool IsZero() const { return FitsU64() && w[0] == 0u; }

    bool Bit(int i) const { return ((w[i >> 6] >> (i & 63)) & 1u) != 0u; }

    int BitLength() const {
        for (int i = N - 1; i >= 0; --i) {
            if (w[i] != 0u) {
                int b = 64;
                while ((w[i] >> (b - 1)) == 0u) --b;
                return i * 64 + b;
            }
        }
        return 0;
    }
};

template <int N, int M>
int CompareWide(const WideUint<N>& a, const WideUint<M>& b) {
    constexpr int K = N > M ? N : M;
    for (int i = K - 1; i >= 0; --i) {
        const uint64_t x = i < N ? a.w[i] : 0u;
        const uint64_t y = i < M ? b.w[i] : 0u;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

template <int N, int M>
WideUint<N> AddWide(WideUint<N> a, const WideUint<M>& b) {
    uint64_t carry = 0u;
    for (int i = 0; i < N; ++i) {
        const uint64_t y   = i < M ? b.w[i] : 0u;
        const uint64_t sum = a.w[i] + y;
        const uint64_t out = sum < y ? 1u : 0u;
        a.w[i] = sum + carry;
        carry  = out | (a.w[i] < carry ? 1u : 0u);
    }
    return a;
}

template <int N, int M>
void SubtractWide(WideUint<N>& a, const WideUint<M>& b) {
    uint64_t borrow = 0u;
    for (int i = 0; i < N; ++i) {
        const uint64_t y    = i < M ? b.w[i] : 0u;
        const uint64_t diff = a.w[i] - y;
        const uint64_t out  = (a.w[i] < y || diff < borrow) ? 1u : 0u;
        a.w[i] = diff - borrow;
        borrow = out;
    }
}

template <int N, int M>
WideUint<N + M> MultiplyWide(const WideUint<N>& a, const WideUint<M>& b) {
    WideUint<N + M> r;
    for (int i = 0; i < N; ++i) {
        uint64_t carry = 0u;
        for (int j = 0; j < M; ++j) {
            uint64_t lo = 0u, hi = 0u;
            cerfengine::Mulu64(a.w[i], b.w[j], &lo, &hi);
            uint64_t t = r.w[i + j] + lo;
            hi += t < lo ? 1u : 0u;
            t  += carry;
            hi += t < carry ? 1u : 0u;
            r.w[i + j] = t;
            carry      = hi;
        }
        r.w[i + M] = carry;
    }
    return r;
}

template <int N>
void ShiftLeftWide(WideUint<N>& a, bool in) {
    uint64_t carry = in ? 1u : 0u;
    for (int i = 0; i < N; ++i) {
        const uint64_t next = a.w[i] >> 63;
        a.w[i] = (a.w[i] << 1) | carry;
        carry  = next;
    }
}

template <int N, int M>
uint64_t DivideWide(const WideUint<N>& num, const WideUint<M>& den,
                    std::type_identity_t<WideUint<M>>* rem) {
    if (num.FitsU64() && den.FitsU64()) {
        if (rem != nullptr) *rem = WideUint<M>::Of(num.w[0] % den.w[0]);
        return num.w[0] / den.w[0];
    }
    WideUint<M + 1> r;
    int bit = num.BitLength() - 1;
    if constexpr (N > 1) {
        WideUint<N - 1> high;
        for (int i = 1; i < N; ++i) high.w[i - 1] = num.w[i];
        if (CompareWide(high, den) < 0) {
            r = high.template Resized<M + 1>();
            if (bit > 63) bit = 63;
        }
    }
    uint64_t q = 0u;
    for (; bit >= 0; --bit) {
        ShiftLeftWide(r, num.Bit(bit));
        if (CompareWide(r, den) >= 0) {
            SubtractWide(r, den);
            if (bit < 64) q |= uint64_t{1} << bit;
        }
    }
    if (rem != nullptr) *rem = r.template Resized<M>();
    return q;
}
