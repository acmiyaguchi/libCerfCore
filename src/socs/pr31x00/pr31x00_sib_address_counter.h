#pragma once

#include <cstdint>

namespace cerfcore {

/* TMPR3911 Figure 13.4.1 (printed p.13-16): a 12-bit ADDR CNT, DMA ADDR = START ADDR +
   ADDR CNT, compared against BUFFER SIZE and BUFFER SIZE / 2. */
class Pr31x00SibAddressCounter {
public:
    static constexpr uint32_t kSpan = 0x1000u;

    explicit Pr31x00SibAddressCounter(uint32_t size) : size_(size & (kSpan - 1u)) {}

    uint32_t Size() const { return size_; }

    /* TMPR3911 §13.4 (printed p.13-15): "the DMA address is continuously incremented ... and
       rolls over back to the start address after the end-of-buffer is reached". */
    uint32_t After(uint32_t c, uint64_t k) const {
        if (c > size_) {
            const uint64_t wrap = kSpan - c;
            if (k < wrap) return static_cast<uint32_t>(c + k);
            k -= wrap;
            c = 0;
        }
        const uint64_t to_end = size_ - c;
        if (k <= to_end) return static_cast<uint32_t>(c + k);
        return static_cast<uint32_t>((k - to_end - 1u) % (size_ + 1u));
    }

    uint64_t NextMatch(uint32_t c) const {
        const uint32_t half = size_ / 2u;
        if (c > size_) return (kSpan - c) + half;
        if (c < half) return half - c;
        if (c < size_) return size_ - c;
        return 1u + half;
    }

private:
    uint32_t size_;
};

}
