#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateReader;
class StateWriter;

class Pxa255I2sFifoResetSync : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Apply(uint32_t sacr1, uint32_t old, uint64_t bits, uint64_t first);
    void Cancel();
    bool TxPending(uint64_t& frame) const;
    bool RxPending(uint64_t& frame) const;
    void TxDone() { tx_.pending = false; }
    void RxDone() { rx_.pending = false; }

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    struct Pending {
        bool     pending    = false;
        uint64_t frame      = 0;
        uint64_t assert_bit = 0;
    };

    static void ApplyOne(uint32_t now_set, uint32_t was_set, uint64_t bits, uint64_t first, Pending& p);

    Pending tx_;
    Pending rx_;
};
