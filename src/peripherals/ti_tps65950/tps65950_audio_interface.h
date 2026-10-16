#pragma once

#include <cerfcore/core/service.h>
#include "../../host/paced_wave_out.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/omap3530/omap3530_mcbsp_link.h"
#include "../../socs/rated_tick_count.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

class StateReader;
class StateWriter;
class Tps65950AudioDownlink;
class Tps65950AudioUplink;
class Twl4030;

class Tps65950AudioInterface : public Service,
                               public Omap3530McBspPeer,
                               public Omap3530McBspClockPad {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void OnAudioRegisterWillChange();
    void OnAudioRegisterWrite();
    void Save(StateWriter& w);
    void Restore(StateReader& r);

    bool                  DrivesClockAndSync() const override { return cfg_.drives_bus; }
    GuestCycleClock::Rate BitRate() const override;
    uint64_t              BitsAt(uint64_t cycle) const override { return bits_.TicksAt(cycle); }
    uint64_t              CycleOfBit(uint64_t bit) const override { return bits_.CycleOfTick(bit); }
    uint32_t              FrameBits() const override { return cfg_.frame_bits; }
    uint32_t              SyncRise() const override;
    uint32_t              SyncFall() const override;
    void                  ReceiveFrame(const Omap3530McBspDxFrame& frame) override;
    void                  FlushFrames() override;
    uint32_t              DoutWord(const Omap3530McBspDxFrame& shape, uint32_t start, uint32_t bits) override;
    void                  SetPeerListener(Omap3530McBspPeerListener* listener) override;

    GuestCycleClock::Rate PadRate() const override;
    void                  RegisterPadListener(std::function<void()> fn) override;

private:
    struct Config {
        uint32_t fs_hz      = 0;
        uint64_t infreq_hz  = 0;
        uint8_t  format     = 0;
        uint32_t slot_bits  = 0;
        uint32_t word_bits  = 0;
        uint32_t frame_bits = 0;
        bool     sink_on    = false;
        bool     drives_bus = false;
        bool     pad_on     = false;
        bool     dout_on    = false;

        bool operator==(const Config&) const = default;
    };

    Config                ReadConfig() const;
    bool                  HighLeft() const;
    GuestCycleClock::Rate ApllScaled(uint64_t nominal_hz) const;
    void                  Apply(const Config& next);
    void   OnCpuRate();
    void   StartHost();

    void    FinishHistory();
    size_t  HostBlockBytes() const;
    void    PushWholeBlocks();
    int64_t WordStart(const Omap3530McBspDxFrame& frame, uint32_t k) const;
    void    Decode(const Omap3530McBspDxFrame& cur, const Omap3530McBspDxFrame* prev,
                   const Omap3530McBspDxFrame* next);
    int16_t    WordAt(int64_t start, const Omap3530McBspDxFrame& cur, const Omap3530McBspDxFrame* prev,
                      const Omap3530McBspDxFrame* next) const;
    static int BitAt(const Omap3530McBspDxFrame* frame, int64_t offset);

    GuestCycleClock*                   clock_    = nullptr;
    Twl4030*                           twl_      = nullptr;
    Tps65950AudioUplink*               uplink_   = nullptr;
    Tps65950AudioDownlink*             downlink_ = nullptr;
    Omap3530McBspPeerListener*         listener_ = nullptr;
    uint64_t                           hfclkin_hz_ = 0;
    std::vector<std::function<void()>> pad_listeners_;

    Config         cfg_;
    RatedTickCount bits_;

    std::deque<Omap3530McBspDxFrame> history_;
    PacedWaveOut                     host_;
    bool                             host_active_ = false;
    std::vector<uint8_t>             block_;
};
