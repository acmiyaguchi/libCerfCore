#include "wave_out_sink.h"

#include "wave_out_callback_loop.h"
#include "wave_out_reopen_policy.h"

#include <cerfcore/core/log.h>

#include <alsa/asoundlib.h>

#include <cerrno>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace cerfcore {

class WaveOutSink::Impl final : private WaveOutDevice {
public:
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
    struct Block {
        uint32_t       slot;
        const uint8_t* bytes;
        uint32_t       length;
    };

    /* QEMU audio/alsaaudio.c: buffer_length 92880 us, "4096 frames assuming 44100Hz". */
    static constexpr unsigned kBufferUs    = 92880;
    static constexpr int      kWaitSliceMs = 100;

    void WriterMain();
    int  WriteBlock(snd_pcm_t* pcm, const Block& block, uint32_t frame_bytes);
    void CompleteQueuedLocked();
    void LoseDeviceLocked(const char* call, int err);
    void RetryOpen();

    bool DeviceOpenLocked() const override { return pcm_ != nullptr; }
    void CloseDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool OpenDeviceLocked(std::unique_lock<std::mutex>& lk) override;

    mutable std::mutex      mtx_;
    std::condition_variable writer_cv_;
    std::condition_variable idle_cv_;
    std::deque<Block>       writes_;
    WaveOutCallbackLoop     callbacks_;
    WaveOutReopenPolicy     policy_;
    snd_pcm_t*              pcm_            = nullptr;
    bool                    writing_        = false;
    bool                    abort_          = false;
    bool                    shutdown_       = false;
    uint32_t                frame_bytes_    = 0;
    uint64_t                frames_written_ = 0;
    uint32_t                generation_     = 0;
    int                     last_fail_      = 0;
    std::thread             writer_thread_;
    const char*             log_tag_ = "WaveOutSink";
};

void WaveOutSink::Impl::Start(StartHandler on_start, CommandHandler on_command,
                              DoneHandler on_done, const char* log_tag) {
    log_tag_       = log_tag;
    writer_thread_ = std::thread([this] { WriterMain(); });
    callbacks_.Start(std::move(on_start), std::move(on_command), std::move(on_done),
                     [this] { RetryOpen(); });
}

void WaveOutSink::Impl::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        shutdown_ = true;
        if (pcm_ != nullptr) snd_pcm_drop(pcm_);
    }
    writer_cv_.notify_all();
    callbacks_.Stop();
    if (writer_thread_.joinable()) writer_thread_.join();
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

void WaveOutSink::Impl::PostCommand(uint32_t command) { callbacks_.PostCommand(command); }

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
        callbacks_.PostDone(block.slot);
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

bool WaveOutSink::Impl::OpenDeviceLocked(std::unique_lock<std::mutex>&) {
    policy_.NoteAttempt();
    const WaveOutFormat& req = policy_.Requested();
    int                  err    = 0;
    snd_pcm_t*           pcm    = nullptr;
    snd_pcm_format_t     format = SND_PCM_FORMAT_UNKNOWN;
    if (req.bits == 16u) format = SND_PCM_FORMAT_S16_LE;
    if (req.bits == 8u) format = SND_PCM_FORMAT_U8;
    if (format == SND_PCM_FORMAT_UNKNOWN) {
        err = -EINVAL;
    } else {
        err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (err == 0) {
            err = snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED, req.channels,
                                     req.rate_hz, req.allow_resampler ? 1 : 0, kBufferUs);
        }
    }
    if (err < 0) {
        if (pcm != nullptr) snd_pcm_close(pcm);
        if (err != last_fail_) {
            LOG(Caution, "%s: ALSA playback open (%u Hz x %u ch x %u bit) failed: %s "
                         "- silent-mode (blocks paced from their own duration)\n",
                log_tag_, req.rate_hz, req.channels, req.bits, snd_strerror(err));
        }
        last_fail_ = err;
        return false;
    }
    pcm_            = pcm;
    frame_bytes_    = static_cast<uint32_t>(req.channels) * (req.bits / 8u);
    frames_written_ = 0;
    last_fail_      = 0;
    policy_.NoteOpened();
    ++generation_;
    LOG(Periph, "[%s] ALSA playback opened %u Hz x %u ch x %u bit\n", log_tag_, req.rate_hz,
        req.channels, req.bits);
    writer_cv_.notify_all();
    return true;
}

void WaveOutSink::Impl::CompleteQueuedLocked() {
    for (const Block& b : writes_) callbacks_.PostDone(b.slot);
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
    last_fail_ = err;
    policy_.NoteAttempt();
}

bool WaveOutSink::Impl::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits,
                                     bool allow_resampler, bool busy) {
    std::unique_lock<std::mutex> lk(mtx_);
    return policy_.EnsureFormat(WaveOutFormat{sample_rate_hz, channels, bits, allow_resampler},
                                busy, *this, lk);
}

void WaveOutSink::Impl::RetryOpen() {
    std::unique_lock<std::mutex> lk(mtx_);
    if (policy_.BeginRetry(pcm_ != nullptr)) OpenDeviceLocked(lk);
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

void WaveOutSink::Impl::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        writes_.push_back(Block{slot, bytes, length});
        writer_cv_.notify_all();
        return;
    }
    const WaveOutFormat& req = policy_.Requested();
    callbacks_.QueueSilent(slot, length, req.rate_hz, req.channels, req.bits, log_tag_);
    if (policy_.ShouldPostRetry()) callbacks_.PostRetryOpen();
}

void WaveOutSink::Impl::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (pcm_ != nullptr) {
        abort_ = true;
        snd_pcm_drop(pcm_);
        CompleteQueuedLocked();
    }
    callbacks_.CompleteSilent();
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
