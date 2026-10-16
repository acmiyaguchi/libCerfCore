#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace cerfcore {

class WaveOutSink {
public:
    static constexpr uint32_t kSlots  = 8;
    static constexpr uint32_t kNoSlot = kSlots;

    using StartHandler   = std::function<void()>;
    using CommandHandler = std::function<void(uint32_t command)>;
    using DoneHandler    = std::function<void(uint32_t slot)>;

    WaveOutSink();
    ~WaveOutSink();
    WaveOutSink(const WaveOutSink&)            = delete;
    WaveOutSink& operator=(const WaveOutSink&) = delete;

    void Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
               const char* log_tag);
    void Stop();
    void PostCommand(uint32_t command);

    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                      uint16_t bits, bool allow_resampler, bool busy);
    bool IsOpen() const;
    bool SamplePosition(uint32_t& samples, uint32_t& generation) const;

    void Play(uint32_t slot, const uint8_t* bytes, uint32_t length);
    void Reset();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
