#pragma once

#include <cstdint>
#include <vector>

enum class Omap3530McBspDir : uint8_t { Tx = 0, Rx = 1 };

class Omap3530McBspFrame {
public:
    enum class Kind : uint8_t { Driven, Masked, Disabled };

    struct Slot {
        uint32_t start = 0;
        uint32_t bits  = 0;
        Kind     kind  = Kind::Driven;

        bool operator==(const Slot&) const = default;
    };

    struct Regs {
        uint32_t xcr1 = 0, xcr2 = 0, rcr1 = 0, rcr2 = 0, mcr1 = 0, mcr2 = 0;
        uint32_t xcer[8] = {};
        uint32_t rcer[8] = {};

        bool operator==(const Regs&) const = default;
    };

    const char* Build(const Regs& regs, Omap3530McBspDir dir);

    static uint32_t Reversed(uint32_t word, uint32_t bits);

    bool operator==(const Omap3530McBspFrame&) const = default;

    bool                     Reverse() const { return reverse_; }
    uint32_t                 DataBits() const { return data_bits_; }
    const std::vector<Slot>& Slots() const { return slots_; }
    uint32_t                 TakeCount() const { return static_cast<uint32_t>(takes_.size()); }
    const Slot&              TakeSlot(uint32_t k) const { return slots_[takes_[k]]; }

private:
    std::vector<Slot>     slots_;
    std::vector<uint32_t> takes_;
    uint32_t              delay_     = 0;
    uint32_t              data_bits_ = 0;
    bool                  reverse_   = false;
};
