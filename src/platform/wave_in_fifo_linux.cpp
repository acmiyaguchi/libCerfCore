#include "wave_in_fifo.h"

#include "capped_byte_fifo.h"
#include "wave_in_request.h"

#include <cerfcore/core/log.h>

#include <alsa/asoundlib.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace cerfcore {

class WaveInFifo::Impl {
public:
    Impl() = default;
    ~Impl() { Stop(); }
    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    void Start(const char* log_tag, size_t cap_bytes) {
        log_tag_ = log_tag;
        fifo_.SetCap(cap_bytes);
        thread_ = std::thread([this] { ThreadMain(); });
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
            if (pcm_ != nullptr) snd_pcm_drop(pcm_);
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
        if (!request_.Set(sample_rate_hz, channels, bits)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        if (pcm_ != nullptr) snd_pcm_drop(pcm_);
        cv_.notify_all();
    }

    void   Clear() { fifo_.Clear(); }
    size_t Take(uint8_t* dst, size_t bytes) { return fifo_.Take(dst, bytes); }

private:
    static constexpr uint32_t kRecordBufBytes = 4096u;
    /* QEMU audio/alsaaudio.c: buffer_length 92880 us, "4096 frames assuming 44100Hz". */
    static constexpr unsigned kBufferUs = 92880;

    void ThreadMain();
    void OpenLocked(const WaveInFormat& format);
    void CloseLocked();

    std::mutex              mtx_;
    std::condition_variable cv_;
    snd_pcm_t*              pcm_         = nullptr;
    uint32_t                frame_bytes_ = 0;
    bool                    stop_        = false;
    WaveInRequest           request_;
    CappedByteFifo          fifo_;
    std::thread             thread_;
    const char*             log_tag_ = "WaveInFifo";
};

void WaveInFifo::Impl::OpenLocked(const WaveInFormat& requested) {
    const uint32_t   rate     = requested.rate_hz;
    const uint16_t   channels = requested.channels;
    const uint16_t   bits     = requested.bits;
    snd_pcm_format_t format   = SND_PCM_FORMAT_UNKNOWN;
    if (bits == 16u) format = SND_PCM_FORMAT_S16_LE;
    if (bits == 8u) format = SND_PCM_FORMAT_U8;
    snd_pcm_t* pcm = nullptr;
    int        err = -EINVAL;
    if (format != SND_PCM_FORMAT_UNKNOWN) {
        err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
        if (err == 0) {
            err = snd_pcm_set_params(pcm, format, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate,
                                     1, kBufferUs);
        }
        if (err == 0) err = snd_pcm_start(pcm);
    }
    if (err < 0) {
        if (pcm != nullptr) snd_pcm_close(pcm);
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (ALSA capture open: %s); "
                     "the guest records silence\n",
            log_tag_, rate, snd_strerror(err));
        return;
    }
    pcm_         = pcm;
    frame_bytes_ = static_cast<uint32_t>(channels) * (bits / 8u);
    LOG(Periph, "[%s] ALSA capture opened %u Hz x %u ch x %u bit\n", log_tag_, rate, channels,
        bits);
}

void WaveInFifo::Impl::CloseLocked() {
    if (pcm_ == nullptr) return;
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
}

/* QEMU audio/alsaaudio.c alsa_read: -EPIPE (overrun) goes to snd_pcm_recover; alsa-lib
   pcm.c snd_pcm_recover leaves the stream prepared, so capture is started again. */
void WaveInFifo::Impl::ThreadMain() {
    std::vector<uint8_t>         buf(kRecordBufBytes);
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        WaveInFormat format;
        if (request_.TakeChange(format)) {
            CloseLocked();
            fifo_.Clear();
            OpenLocked(format);
            continue;
        }
        if (pcm_ == nullptr) {
            cv_.wait(lk, [&] { return stop_ || request_.Pending(); });
            continue;
        }
        snd_pcm_t* const        pcm         = pcm_;
        const uint32_t          frame_bytes = frame_bytes_;
        lk.unlock();
        const snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf.data(), kRecordBufBytes / frame_bytes);
        if (n > 0) fifo_.Push(buf.data(), static_cast<size_t>(n) * frame_bytes);
        lk.lock();
        if (n >= 0 || stop_ || request_.Pending()) continue;
        int err = snd_pcm_recover(pcm, static_cast<int>(n), 1);
        if (err == 0) err = snd_pcm_start(pcm);
        if (err < 0) {
            LOG(Caution, "[%s] host microphone lost (snd_pcm_readi: %s); the guest records "
                         "silence\n",
                log_tag_, snd_strerror(err));
            CloseLocked();
        }
    }
    CloseLocked();
}

WaveInFifo::WaveInFifo() : impl_(std::make_unique<Impl>()) {}
WaveInFifo::~WaveInFifo() = default;

void WaveInFifo::Start(const char* log_tag, size_t cap_bytes) { impl_->Start(log_tag, cap_bytes); }
void WaveInFifo::Stop() { impl_->Stop(); }
void WaveInFifo::Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    impl_->Request(sample_rate_hz, channels, bits);
}
void WaveInFifo::Clear() { impl_->Clear(); }
size_t WaveInFifo::Take(uint8_t* dst, size_t bytes) { return impl_->Take(dst, bytes); }

}
