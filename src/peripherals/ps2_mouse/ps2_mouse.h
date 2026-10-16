#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

class StateWriter;
class StateReader;

/* Generic PS/2 mouse device: the standard command/response handshake and
   3-byte motion packet, independent of any host controller. A board maps its
   controller's data/status registers onto WriteCommand / HasData / ReadData,
   and supplies on_data to raise its IRQ when a motion packet is queued. */
class Ps2Mouse {
public:
    static constexpr uint32_t kButtonLeft  = 0x1u;
    static constexpr uint32_t kButtonRight = 0x2u;

    explicit Ps2Mouse(std::function<void()> on_data)
        : on_data_(std::move(on_data)) {}

    /* Driver -> device: a command byte (or a parameter for the previous
       command). Queues the ACK + any command-specific response. */
    void WriteCommand(uint8_t cmd);

    bool    HasData() const;   /* a response/packet byte is pending */
    uint8_t ReadData();        /* device -> driver, pops one byte */

    /* Host motion -> a 3-byte packet + the data IRQ. dx>0 right, dy>0 down. */
    void QueueMotion(int dx, int dy, uint32_t button_mask);

    /* reporting_/expect_param_ are guest-set modes that persist; the motion
       packet queue is transient host input and is cleared on restore. */
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

    /* Power-cycle reset: drop queued motion + return to defaults. A real PS/2
       mouse comes back empty after a system reset; the guest re-inits with F6+F4
       (no FF reset), so a leftover motion packet ahead of the F6 ACK desyncs it. */
    void Reset();

private:
    void PushLocked(uint8_t b) { out_.push_back(b); }
    void RaiseData();   /* invoke on_data_ (IRQ) outside the queue lock */

    mutable std::mutex    mtx_;
    std::deque<uint8_t>   out_;
    bool                  expect_param_ = false;
    bool                  reporting_    = false;  /* 0xF4 enables motion stream + IRQs */
    std::function<void()> on_data_;
};
