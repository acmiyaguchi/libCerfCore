#include "wave_out_sink.h"

#include "silent_block_pacer.h"

#include <cerfcore/core/log.h>

#include <alsa/asoundlib.h>

#include <cerrno>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#if !defined(__linux__)
#error "wave_out_sink_posix.cpp has no audio output for this host"
#endif

namespace cerfcore {

class WaveOutSink::Impl {
public:
    using Clock = SilentBlockPacer::Clock;

    Impl() = default;
    ~Impl() { Stop(); }
    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    void Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
               const char* log_tag);
    void Stop();
    void PostCommand(uint32_t command);

    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits,
                      bool allow_resampler, bool busy);
    bool IsOpen() const;
    bool SamplePosition(uint32_t& samples, uint32_t& generation) const;

    void Play(uint32_t slot, const uint8_t* bytes, uint32_t length);
    void Reset();

private:
    struct Event {
        enum Kind : uint8_t { kCommand, kDone, kRetryOpen } kind;
        uint32_t value;
    };

    struct Block {
        uint32_t       slot;
        const uint8_t* bytes;
        uint32_t       length;
    };

    static constexpr Clock::duration kReopenInterval = std::chrono::seconds(1);
    /* QEMU audio/alsaaudio.c: buffer_length 92880 us, "4096 frames assuming 44100Hz". */
    static constexpr unsigned kBufferUs    = 92880;
    static constexpr int      kWaitSliceMs = 100;

    void CallbackMain(StartHandler on_start, CommandHandler on_command, DoneHandler on_done);
    void WriterMain();
    int  WriteBlock(snd_pcm_t* pcm, const Block& block, uint32_t frame_bytes);
    void PostLocked(Event event);
    bool FormatMatchesLocked() const;
    bool OpenLocked();
    void CloseDeviceLocked(std::unique_lock<std::mutex>& lk);
    void CompleteQueuedLocked();
    void LoseDeviceLocked(const char* call, int err);
    void QueueSilentLocked(uint32_t slot, uint32_t length);
    void RetryOpen();

    mutable std::mutex      mtx_;
    std::condition_variable event_cv_;
    std::condition_variable writer_cv_;
    std::condition_variable idle_cv_;
    std::deque<Event>       events_;
    std::deque<Block>       writes_;
    SilentBlockPacer        silent_;
    snd_pcm_t*              pcm_            = nullptr;
    bool                    writing_        = false;
    bool                    abort_          = false;
    bool                    shutdown_       = false;
    bool                    retry_posted_   = false;
    uint32_t                req_rate_       = 0;
    uint16_t                req_channels_   = 0;
    uint16_t                req_bits_       = 0;
    bool                    req_resample_   = false;
    uint32_t                open_rate_      = 0;
    uint16_t                open_channels_  = 0;
    uint16_t                open_bits_      = 0;
    uint32_t                frame_bytes_    = 0;
    uint64_t                frames_written_ = 0;
    uint32_t                generation_     = 0;
    int                     last_fail_      = 0;
    Clock::time_point       last_attempt_{};
    std::thread             callback_thread_;
    std::thread             writer_thread_;
    const char*             log_tag_ = "WaveOutSink";
};

void WaveOutSink::Impl::Start(StartHandler on_start, CommandHandler on_command,
                              DoneHandler on_done, const char* log_tag) {
    log_tag_         = log_tag;
    writer_thread_   = std::thread([this] { WriterMain(); });
    callback_thread_ = std::thread([this, on_start, on_command, on_done] {
        CallbackMain(on_start, on_command, on_done);
    });
}

void WaveOutSink::Impl::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        shutdown_ = true;
        if (pcm_ != nullptr) snd_pcm_drop(pcm_);
    }
    event_cv_.notify_all();
    writer_cv_.notify_all();
    if (callback_thread_.joinable()) callback_thread_.join();
    if (writer_thread_.joinable()) writer_thread_.join();
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

void WaveOutSink::Impl::PostLocked(Event event) {
    events_.push_back(event);
    event_cv_.notify_all();
}

void WaveOutSink::Impl::PostCommand(uint32_t command) {
    std::lock_guard<std::mutex> lk(mtx_);
    PostLocked(Event{Event::kCommand, command});
}

void WaveOutSink::Impl::CallbackMain(StartHandler on_start, CommandHandler on_command,
                                     DoneHandler on_done) {
    if (on_start) on_start();
    std::unique_lock<std::mutex> lk(mtx_);
    while (!shutdown_) {
        uintptr_t      due[SilentBlockPacer::kCapacity] = {};
        const uint32_t n = silent_.TakeDue(Clock::now(), due);
        if (n == 0u && events_.empty()) {
            if (silent_.Empty()) {
                event_cv_.wait(lk);
            } else {
                event_cv_.wait_until(lk, silent_.NextDue());
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
            } else {
                RetryOpen();
            }
        }
        lk.lock();
    }
}

void WaveOutSink::Impl::WriterMain() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (true) {
        writer_cv_.wait(lk, [this] { return shutdown_ || (!writes_.empty() && pcm_ != nullptr); });
        if (shutdown_) return;
        const Block block = writes_.front();
        writes_.pop_front();
        snd_pcm_t* const pcm         = pcm_;
        const uint32_t   frame_bytes = frame_bytes_;
        writing_                     = true;
        abort_                       = false;
        lk.unlock();
        const int err = WriteBlock(pcm, block, frame_bytes);
        lk.lock();
        writing_ = false;
        idle_cv_.notify_all();
        PostLocked(Event{Event::kDone, block.slot});
        if (err < 0 && !abort_ && pcm_ == pcm) LoseDeviceLocked("snd_pcm_writei", err);
    }
}

/* alsa-lib pcm.c snd_pcm_writei: -EBADFD outside PREPARED/RUNNING, -EPIPE/-ESTRPIPE for
   snd_pcm_recover; QEMU audio/alsaaudio.c alsa_write treats -EAGAIN as a full ring. */
int WaveOutSink::Impl::WriteBlock(snd_pcm_t* pcm, const Block& block, uint32_t frame_bytes) {
    const uint8_t*    src    = block.bytes;
    snd_pcm_uframes_t frames = block.length / frame_bytes;
    while (frames != 0u) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (abort_ || shutdown_) return 0;
        const snd_pcm_sframes_t n = snd_pcm_writei(pcm, src, frames);
        if (n > 0) {
            frames_written_ += static_cast<uint64_t>(n);
            src += static_cast<size_t>(n) * frame_bytes;
            frames -= static_cast<snd_pcm_uframes_t>(n);
            continue;
        }
        if (n == -EAGAIN || n == 0) {
            lk.unlock();
            const int ready = snd_pcm_wait(pcm, kWaitSliceMs);
            if (ready >= 0) continue;
            lk.lock();
            if (abort_ || shutdown_) return 0;
            const int recovered = snd_pcm_recover(pcm, ready, 1);
            if (recovered < 0) return recovered;
            continue;
        }
        const int err = n == -EBADFD ? snd_pcm_prepare(pcm)
                                     : snd_pcm_recover(pcm, static_cast<int>(n), 1);
        if (err < 0) return err;
    }
    return 0;
}

bool WaveOutSink::Impl::FormatMatchesLocked() const {
    return pcm_ != nullptr && open_rate_ == req_rate_ && open_channels_ == req_channels_ &&
           open_bits_ == req_bits_;
}

bool WaveOutSink::Impl::OpenLocked() {
    last_attempt_ = Clock::now();
    int              err    = 0;
    snd_pcm_t*       pcm    = nullptr;
    snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;
    if (req_bits_ == 16u) format = SND_PCM_FORMAT_S16_LE;
    if (req_bits_ == 8u) format = SND_PCM_FORMAT_U8;
    if (format == SND_PCM_FORMAT_UNKNOWN) {
        err = -EINVAL;
    } else {
        err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (err == 0) {
            err = snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED, req_channels_,
                                     req_rate_, req_resample_ ? 1 : 0, kBufferUs);
        }
    }
    if (err < 0) {
        if (pcm != nullptr) snd_pcm_close(pcm);
        if (err != last_fail_) {
            LOG(Caution, "%s: ALSA playback open (%u Hz x %u ch x %u bit) failed: %s "
                         "- silent-mode (blocks paced from their own duration)\n",
                log_tag_, req_rate_, req_channels_, req_bits_, snd_strerror(err));
        }
        last_fail_ = err;
        return false;
    }
    pcm_            = pcm;
    open_rate_      = req_rate_;
    open_channels_  = req_channels_;
    open_bits_      = req_bits_;
    frame_bytes_    = static_cast<uint32_t>(req_channels_) * (req_bits_ / 8u);
    frames_written_ = 0;
    last_fail_      = 0;
    ++generation_;
    LOG(Periph, "[%s] ALSA playback opened %u Hz x %u ch x %u bit\n", log_tag_, req_rate_,
        req_channels_, req_bits_);
    writer_cv_.notify_all();
    return true;
}

void WaveOutSink::Impl::CompleteQueuedLocked() {
    for (const Block& b : writes_) PostLocked(Event{Event::kDone, b.slot});
    writes_.clear();
}

void WaveOutSink::Impl::CloseDeviceLocked(std::unique_lock<std::mutex>& lk) {
    abort_ = true;
    snd_pcm_drop(pcm_);
    idle_cv_.wait(lk, [this] { return !writing_; });
    CompleteQueuedLocked();
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

void WaveOutSink::Impl::LoseDeviceLocked(const char* call, int err) {
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
    CompleteQueuedLocked();
    LOG(Caution, "%s: host audio device lost (%s: %s) - silent-mode (blocks paced from their "
                 "own duration)\n",
        log_tag_, call, snd_strerror(err));
    last_fail_    = err;
    last_attempt_ = Clock::now();
}

bool WaveOutSink::Impl::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits,
                                     bool allow_resampler, bool busy) {
    std::unique_lock<std::mutex> lk(mtx_);
    const bool changed = sample_rate_hz != req_rate_ || channels != req_channels_ ||
                         bits != req_bits_ || allow_resampler != req_resample_;
    req_rate_     = sample_rate_hz;
    req_channels_ = channels;
    req_bits_     = bits;
    req_resample_ = allow_resampler;
    if (FormatMatchesLocked()) return true;
    if (pcm_ != nullptr) {
        if (busy) return true;
        CloseDeviceLocked(lk);
    }
    if (!changed && Clock::now() - last_attempt_ < kReopenInterval) return false;
    return OpenLocked();
}

void WaveOutSink::Impl::RetryOpen() {
    std::lock_guard<std::mutex> lk(mtx_);
    retry_posted_ = false;
    if (pcm_ != nullptr || Clock::now() - last_attempt_ < kReopenInterval) return;
    OpenLocked();
}

bool WaveOutSink::Impl::IsOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return pcm_ != nullptr;
}

/* alsa-lib pcm.c snd_pcm_delay: the frames between the write call and the DAC. */
bool WaveOutSink::Impl::SamplePosition(uint32_t& samples, uint32_t& generation) const {
    std::lock_guard<std::mutex> lk(mtx_);
    generation = generation_;
    if (pcm_ == nullptr) return false;
    snd_pcm_sframes_t delay = 0;
    if (snd_pcm_delay(pcm_, &delay) < 0 || delay < 0 ||
        static_cast<uint64_t>(delay) > frames_written_) {
        return false;
    }
    samples = static_cast<uint32_t>(frames_written_ - static_cast<uint64_t>(delay));
    return true;
}

void WaveOutSink::Impl::QueueSilentLocked(uint32_t slot, uint32_t length) {
    if (silent_.Queue(slot, length, req_rate_, req_channels_, req_bits_, log_tag_)) {
        event_cv_.notify_all();
    }
}

void WaveOutSink::Impl::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        writes_.push_back(Block{slot, bytes, length});
        writer_cv_.notify_all();
        return;
    }
    QueueSilentLocked(slot, length);
    if (!retry_posted_ && Clock::now() - last_attempt_ >= kReopenInterval) {
        retry_posted_ = true;
        PostLocked(Event{Event::kRetryOpen, 0u});
    }
}

void WaveOutSink::Impl::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        abort_ = true;
        snd_pcm_drop(pcm_);
        CompleteQueuedLocked();
    }
    uintptr_t      done[SilentBlockPacer::kCapacity] = {};
    const uint32_t n = silent_.TakeAll(done);
    for (uint32_t i = 0; i < n; ++i) PostLocked(Event{Event::kDone, static_cast<uint32_t>(done[i])});
}

WaveOutSink::WaveOutSink() : impl_(std::make_unique<Impl>()) {}
WaveOutSink::~WaveOutSink() = default;

void WaveOutSink::Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
                        const char* log_tag) {
    impl_->Start(std::move(on_start), std::move(on_command), std::move(on_done), log_tag);
}
void WaveOutSink::Stop() { impl_->Stop(); }
void WaveOutSink::PostCommand(uint32_t command) { impl_->PostCommand(command); }
bool WaveOutSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits,
                               bool allow_resampler, bool busy) {
    return impl_->EnsureFormat(sample_rate_hz, channels, bits, allow_resampler, busy);
}
bool WaveOutSink::IsOpen() const { return impl_->IsOpen(); }
bool WaveOutSink::SamplePosition(uint32_t& samples, uint32_t& generation) const {
    return impl_->SamplePosition(samples, generation);
}
void WaveOutSink::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    impl_->Play(slot, bytes, length);
}
void WaveOutSink::Reset() { impl_->Reset(); }

}
