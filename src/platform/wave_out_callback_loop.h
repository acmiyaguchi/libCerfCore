#pragma once

#include "silent_block_pacer.h"
#include "wave_out_sink.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace cerfcore {

class WaveOutCallbackLoop {
public:
    using RetryHandler = std::function<void()>;

    WaveOutCallbackLoop() = default;
    ~WaveOutCallbackLoop() { Stop(); }
    WaveOutCallbackLoop(const WaveOutCallbackLoop&)            = delete;
    WaveOutCallbackLoop& operator=(const WaveOutCallbackLoop&) = delete;

    void Start(WaveOutSink::StartHandler on_start, WaveOutSink::CommandHandler on_command,
               WaveOutSink::DoneHandler on_done, RetryHandler on_retry_open);
    void Stop();

    void PostCommand(uint32_t command);
    void PostDone(uint32_t slot);
    void PostRetryOpen();
    void QueueSilent(uint32_t slot, uint32_t length, uint32_t rate_hz, uint16_t channels,
                     uint16_t bits, const char* log_tag);
    void CompleteSilent();

private:
    struct Event {
        enum Kind : uint8_t { kCommand, kDone, kRetryOpen } kind;
        uint32_t value;
    };

    void Main(WaveOutSink::StartHandler on_start, WaveOutSink::CommandHandler on_command,
              WaveOutSink::DoneHandler on_done, RetryHandler on_retry_open);
    void PostLocked(Event event);

    std::mutex              mtx_;
    std::condition_variable cv_;
    std::deque<Event>       events_;
    SilentBlockPacer        silent_;
    bool                    shutdown_ = false;
    std::thread             thread_;
};

}
