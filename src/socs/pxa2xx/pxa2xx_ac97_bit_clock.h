#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>
#include <vector>

class Ac97Codec;
class StateReader;
class StateWriter;

class Pxa2xxAc97BitClock : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetChangeListener(std::function<void(uint64_t at)> fn) { on_change_ = std::move(fn); }

    bool                  Running() const;
    bool                  Internal() const { return internal_; }
    GuestCycleClock::Rate Rate() const;
    GuestCycleClock::Rate PadClock() const;
    void                  AddPadListener(std::function<void(uint64_t at)> fn);

    void ColdStart(uint64_t now);
    void Stop(uint64_t now);
    void ResetLine() { codec_bitclk_ = false; }
    bool WarmReset(uint64_t now);
    void CodecPoweredDown(uint64_t at);
    void CodecPoweredUp(uint64_t at);

    void SetUnitClock(uint64_t now, bool on);
    void SetInternal(uint64_t now, bool on);

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    static constexpr uint64_t kInternalHz = 13000000u;

    void Start(uint64_t now);
    void SetCodecDriving(uint64_t at, bool on);

    Ac97Codec*                        codec_ = nullptr;
    std::function<void(uint64_t)>     on_change_;
    std::vector<std::function<void(uint64_t)>> pad_listeners_;

    bool codec_bitclk_ = false;
    bool unit_clock_   = true;
    bool internal_     = false;
};
