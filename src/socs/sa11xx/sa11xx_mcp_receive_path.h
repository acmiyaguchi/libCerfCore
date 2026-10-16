#pragma once

#include "sa11xx_dma_receive_fifo.h"
#include "sa11xx_mcp_receive_schedule.h"
#include "sa11xx_mcp_sample_counter.h"

#include <cstdint>
#include <deque>
#include <vector>

class StateReader;
class StateWriter;

class Sa11xxMcpReceivePath : public Sa11xxDmaReceiveFifo {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    struct LoopFuture {
        const Sa11xxMcpSampleCounter* counter    = nullptr;
        uint64_t                      period     = 1;
        uint64_t                      moved_left = 0;
        bool                          adm        = false;
    };

    void Reset();

    void     Settle(uint64_t tick, bool counting);
    uint16_t Pop();

    void OnCodecWrite(bool adm, bool on, bool in, bool was_in, uint64_t carry, uint64_t period);
    void Rephase(bool adm, uint64_t at, uint64_t old_period, uint64_t period, uint64_t tick);
    void SwitchMode(bool adm, uint64_t cut, uint64_t period, bool in, bool on,
                    const Sa11xxMcpSampleCounter& counter);
    void Freeze();
    void Resume(uint64_t frozen, uint64_t base, bool adm, uint64_t period,
                const Sa11xxMcpSampleCounter& counter);
    void SetLooped(bool looped, uint64_t edge, bool adm, uint64_t period,
                   const Sa11xxMcpSampleCounter& counter);
    void LoopTakes(const std::vector<uint64_t>& zeros, const std::vector<uint16_t>& taken,
                   uint16_t before, bool adm);

    bool TickOfMoved(uint64_t words, const LoopFuture& f, uint64_t& tick, bool& now) const;
    bool NextRise(bool are, uint32_t request_at, const LoopFuture& f, uint64_t& tick) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

protected:
    explicit Sa11xxMcpReceivePath(CerfEmulator& emu);

private:
    struct LoopPush {
        uint64_t tick  = 0;
        uint16_t value = 0;
    };

    void Open(uint64_t origin, uint64_t period, uint64_t from);
    void OpenEveryValidFrame(uint64_t from);
    void Reopen(uint64_t edge, bool adm, uint64_t period, const Sa11xxMcpSampleCounter& counter);
    bool TickOfPush(uint64_t index, const LoopFuture& f, uint64_t& tick) const;

    Sa11xxMcpReceiveSchedule schedule_;
    std::deque<LoopPush>     loop_;
    uint64_t pushed_     = 0;
    uint64_t first_conv_ = kNever;
    bool     armed_      = false;
    bool     resume_     = false;
    bool     looped_     = false;
    bool     loop_armed_ = false;
};
