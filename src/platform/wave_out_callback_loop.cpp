#include "wave_out_callback_loop.h"

#include <utility>

namespace cerfcore {

void WaveOutCallbackLoop::Start(WaveOutSink::StartHandler on_start,
                                WaveOutSink::CommandHandler on_command,
                                WaveOutSink::DoneHandler on_done, RetryHandler on_retry_open) {
    thread_ = std::thread([this, on_start = std::move(on_start), on_command = std::move(on_command),
                           on_done = std::move(on_done),
                           on_retry_open = std::move(on_retry_open)] {
        Main(on_start, on_command, on_done, on_retry_open);
    });
}

void WaveOutCallbackLoop::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        shutdown_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WaveOutCallbackLoop::PostLocked(Event event) {
    events_.push_back(event);
    cv_.notify_all();
}

void WaveOutCallbackLoop::PostCommand(uint32_t command) {
    std::lock_guard<std::mutex> lk(mtx_);
    PostLocked(Event{Event::kCommand, command});
}

void WaveOutCallbackLoop::PostDone(uint32_t slot) {
    std::lock_guard<std::mutex> lk(mtx_);
    PostLocked(Event{Event::kDone, slot});
}

void WaveOutCallbackLoop::PostRetryOpen() {
    std::lock_guard<std::mutex> lk(mtx_);
    PostLocked(Event{Event::kRetryOpen, 0u});
}

void WaveOutCallbackLoop::QueueSilent(uint32_t slot, uint32_t length, uint32_t rate_hz,
                                      uint16_t channels, uint16_t bits, const char* log_tag) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (silent_.Queue(slot, length, rate_hz, channels, bits, log_tag)) cv_.notify_all();
}

void WaveOutCallbackLoop::CompleteSilent() {
    std::lock_guard<std::mutex> lk(mtx_);
    uintptr_t      done[SilentBlockPacer::kCapacity] = {};
    const uint32_t n = silent_.TakeAll(done);
    for (uint32_t i = 0; i < n; ++i) PostLocked(Event{Event::kDone, static_cast<uint32_t>(done[i])});
}

void WaveOutCallbackLoop::Main(WaveOutSink::StartHandler on_start,
                               WaveOutSink::CommandHandler on_command,
                               WaveOutSink::DoneHandler on_done, RetryHandler on_retry_open) {
    if (on_start) on_start();
    std::unique_lock<std::mutex> lk(mtx_);
    while (!shutdown_) {
        uintptr_t      due[SilentBlockPacer::kCapacity] = {};
        const uint32_t n = silent_.TakeDue(SilentBlockPacer::Clock::now(), due);
        if (n == 0u && events_.empty()) {
            if (silent_.Empty()) {
                cv_.wait(lk);
            } else {
                cv_.wait_until(lk, silent_.NextDue());
            }
            continue;
        }
        std::deque<Event> events;
        events.swap(events_);
        lk.unlock();
        for (uint32_t i = 0; i < n; ++i) {
            if (on_done) on_done(static_cast<uint32_t>(due[i]));
        }
        for (const Event& e : events) {
            if (e.kind == Event::kCommand) {
                if (on_command) on_command(e.value);
            } else if (e.kind == Event::kDone) {
                if (on_done) on_done(e.value);
            } else if (on_retry_open) {
                on_retry_open();
            }
        }
        lk.lock();
    }
}

}
