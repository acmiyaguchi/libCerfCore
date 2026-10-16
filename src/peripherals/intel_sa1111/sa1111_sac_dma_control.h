#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Sa1111SacDmaControl {
public:
    struct Fifo {
        bool     enabled         = false;
        uint32_t threshold_level = 0;
        bool     loop_back       = false;
    };

    struct Keys {
        const char* control;
        const char* address_a;
        const char* count_a;
        const char* address_b;
        const char* count_b;
    };

    explicit Sa1111SacDmaControl(const Keys& keys) : keys_(keys) {}

    uint32_t Read(uint32_t index) const { return regs_[index]; }
    void     WriteControl(uint32_t value);
    void     WriteBuffer(uint32_t index, uint32_t value);

    bool     Enabled() const          { return (regs_[kControl] & kEn) != 0u; }
    bool     InterruptEnabled() const { return (regs_[kControl] & kIe) != 0u; }
    uint32_t Address(bool buffer_b) const;
    uint32_t Bytes(bool buffer_b) const;

    bool NextBuffer(bool prefer_b, bool& buffer_b);
    void Begin(bool buffer_b);
    void Idle();
    bool Complete(bool buffer_b, bool keep_started);
    void Reset();

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    enum : uint32_t { kControl = 0, kAddressA = 1, kCountA = 2, kAddressB = 3, kCountB = 4, kRegs = 5 };
    enum : uint32_t {
        kEn  = 1u << 0,
        kIe  = 1u << 1,
        kBda = 1u << 3,
        kSta = 1u << 4,
        kBdb = 1u << 5,
        kStb = 1u << 6,
        kBiu = 1u << 7,
    };

    Keys     keys_;
    uint32_t regs_[kRegs] = {};
};

}
