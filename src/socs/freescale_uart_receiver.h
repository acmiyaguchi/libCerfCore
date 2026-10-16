#pragma once

#include "freescale_uart_regs.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

struct FreescaleUartRxConfig {
    cerf_freescale_uart::FreescaleUartFrame frame;
    bool     enabled    = false;
    bool     loop       = false;
    bool     hold       = false;
    bool     iden       = false;
    bool     ir         = false;
    bool     invr       = false;
    uint32_t rxtl       = 1;
    uint32_t icd_frames = 4;
};

inline FreescaleUartRxConfig RxConfigOf(
    const uint32_t (&ctrl)[cerf_freescale_uart::kCtrlCount], bool hold) {
    using namespace cerf_freescale_uart;
    const uint32_t ucr1 = CtrlOf(ctrl, kOffUcr1), ucr2 = CtrlOf(ctrl, kOffUcr2);
    FreescaleUartRxConfig c;
    c.frame      = FrameOf(ucr2);
    c.enabled    = (ucr1 & kUcr1Uarten) != 0u && (ucr2 & kUcr2Rxen) != 0u;
    c.loop       = RxInputLooped(ctrl);
    c.hold       = hold;
    c.iden       = (ucr1 & kUcr1Iden) != 0u;
    c.ir         = IrMode(ctrl);
    c.invr       = (CtrlOf(ctrl, kOffUcr4) & kUcr4Invr) != 0u;
    c.rxtl       = RxtlOf(CtrlOf(ctrl, kOffUfcr));
    c.icd_frames = 4u << ((ucr1 >> kUcr1IcdShift) & 0x3u);
    return c;
}

struct FreescaleUartRxEvent {
    enum class Kind : uint8_t { kStart, kRise, kFall, kStored };
    Kind     kind;
    uint64_t tick;
    uint16_t entry;
};

class FreescaleUartReceiver {
public:
    static constexpr uint64_t kNever = UINT64_MAX;
    using Events = std::vector<FreescaleUartRxEvent>;

    void Reset(bool drop_line);
    void Configure(uint64_t tick, const FreescaleUartRxConfig& cfg);
    void Inject(uint64_t tick, const uint8_t* data, size_t n);
    void LoopChar(uint64_t start, uint8_t byte, bool parity_error, Events& out);
    void LoopBreak(uint64_t start, Events& out);
    void LoopBreakEnd(uint64_t tick);
    void EdgeWithoutClock();
    void Advance(uint64_t tick, Events& out);
    uint16_t Pop(uint64_t tick);
    void     ClearFlags(uint64_t tick, uint32_t usr1, uint32_t usr2);
    uint32_t Take(uint64_t tick, uint32_t max, uint8_t* out);

    uint64_t NextTick() const;
    uint32_t Level() const { return static_cast<uint32_t>(fifo_.size()); }
    uint32_t Usr1() const;
    uint32_t Usr2() const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    struct Char {
        uint8_t byte  = 0;
        uint8_t flags = 0;
    };
    static constexpr uint8_t kParity   = 1u << 0;
    static constexpr uint8_t kBreak    = 1u << 1;
    static constexpr uint8_t kEdgeSeen = 1u << 2;

    enum class Stage : uint8_t { kWake, kRise, kFall, kStore, kEnd };

    void     Begin(uint64_t start, const Char& c, bool pin, Events& out);
    void     Store(uint64_t tick, Events& out);
    void     Counting(uint64_t tick);
    void     Emptied(uint64_t tick);
    void     SkipAbsentStages();
    uint64_t StageTick() const;
    uint64_t NextStart() const;
    uint64_t AgingDue() const;
    uint64_t IdleDue() const;

    std::deque<Char>     wire_;
    std::deque<uint16_t> fifo_;
    cerf_freescale_uart::FreescaleUartFrame frame_;
    bool     enabled_     = false;
    bool     loop_        = false;
    bool     hold_        = false;
    bool     ir_          = false;
    bool     invr_        = false;
    uint32_t rxtl_        = 1;
    uint32_t icd_frames_  = 4;
    bool     iden_        = false;
    bool     busy_        = false;
    Char     cur_;
    Stage    stage_       = Stage::kWake;
    uint64_t start_       = 0;
    uint64_t rise_        = kNever;
    uint64_t fall_        = kNever;
    uint32_t cur_bits_    = 10;
    uint32_t cur_data_    = 8;
    bool     cur_lost_    = false;
    bool     cur_pin_     = false;
    uint64_t wire_from_   = 0;
    uint64_t line_free_   = 0;
    bool     line_low_    = false;
    bool     counting_    = false;
    uint64_t anchor_      = 0;
    uint64_t idle_from_   = kNever;
    bool     idle_wait_   = false;
    bool     agtim_       = false;
    bool     idle_        = false;
    bool     wake_        = false;
    bool     awake_       = false;
    bool     airint_      = false;
    bool     irint_       = false;
    bool     ore_         = false;
    bool     brcd_        = false;
    bool     framerr_     = false;
    bool     parityerr_   = false;
};

}
