#include "wave_in_fifo.h"

#include "capped_byte_fifo.h"
#include "wave_in_request.h"

#include <cerfcore/core/log.h>

#include <AudioToolbox/AudioToolbox.h>

#include <condition_variable>
#include <mutex>
#include <thread>

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
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
        if (!request_.Set(sample_rate_hz, channels, bits)) return;
        std::lock_guard<std::mutex> lk(mtx_);
        cv_.notify_all();
    }

    void   Clear() { fifo_.Clear(); }
    size_t Take(uint8_t* dst, size_t bytes) { return fifo_.Take(dst, bytes); }

private:
    static constexpr uint32_t kRecordBufBytes = 4096u;
    static constexpr uint32_t kRecordBuffers  = 3u;

    static void Input(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer,
                      const AudioTimeStamp* start, UInt32 packets,
                      const AudioStreamPacketDescription* descriptions);
    void ThreadMain();
    void OpenLocked(const WaveInFormat& format);
    void CloseLocked();

    std::mutex              mtx_;
    std::condition_variable cv_;
    AudioQueueRef           queue_ = nullptr;
    bool                    stop_  = false;
    WaveInRequest           request_;
    CappedByteFifo          fifo_;
    std::thread             thread_;
    const char*             log_tag_ = "WaveInFifo";
};

void WaveInFifo::Impl::Input(void* user, AudioQueueRef queue, AudioQueueBufferRef buffer,
                             const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
    Impl* const self = static_cast<Impl*>(user);
    if (buffer->mAudioDataByteSize != 0u) {
        self->fifo_.Push(static_cast<const uint8_t*>(buffer->mAudioData), buffer->mAudioDataByteSize);
    }
    ::AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void WaveInFifo::Impl::OpenLocked(const WaveInFormat& requested) {
    const uint32_t rate     = requested.rate_hz;
    const uint16_t channels = requested.channels;
    const uint16_t bits     = requested.bits;
    const uint32_t frame    = static_cast<uint32_t>(channels) * (bits / 8u);
    OSStatus       err      = kAudio_ParamError;
    const char*    step     = "format";
    AudioQueueRef  queue    = nullptr;
    if ((bits == 8u || bits == 16u) && channels != 0u && rate != 0u) {
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate       = rate;
        fmt.mFormatID         = kAudioFormatLinearPCM;
        fmt.mFormatFlags      = (bits == 16u ? kAudioFormatFlagIsSignedInteger : 0u) |
                                kAudioFormatFlagIsPacked;
        fmt.mBytesPerPacket   = frame;
        fmt.mFramesPerPacket  = 1u;
        fmt.mBytesPerFrame    = frame;
        fmt.mChannelsPerFrame = channels;
        fmt.mBitsPerChannel   = bits;
        step = "AudioQueueNewInput";
        err  = ::AudioQueueNewInput(&fmt, &Input, this, nullptr, nullptr, 0, &queue);
        for (uint32_t i = 0; err == noErr && i < kRecordBuffers; ++i) {
            AudioQueueBufferRef buffer = nullptr;
            step = "AudioQueueAllocateBuffer";
            err  = ::AudioQueueAllocateBuffer(queue, kRecordBufBytes / frame * frame, &buffer);
            if (err == noErr) {
                step = "AudioQueueEnqueueBuffer";
                err  = ::AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
            }
        }
        if (err == noErr) {
            step = "AudioQueueStart";
            err  = ::AudioQueueStart(queue, nullptr);
        }
    }
    if (err != noErr) {
        if (queue != nullptr) ::AudioQueueDispose(queue, true);
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (CoreAudio %s: OSStatus %d); "
                     "the guest records silence\n",
            log_tag_, rate, step, static_cast<int>(err));
        return;
    }
    queue_ = queue;
    LOG(Periph, "[%s] CoreAudio capture opened %u Hz x %u ch x %u bit\n", log_tag_, rate, channels,
        bits);
}

void WaveInFifo::Impl::CloseLocked() {
    if (queue_ == nullptr) return;
    ::AudioQueueStop(queue_, true);
    ::AudioQueueDispose(queue_, true);
    queue_ = nullptr;
}

void WaveInFifo::Impl::ThreadMain() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        WaveInFormat format;
        if (request_.TakeChange(format)) {
            CloseLocked();
            fifo_.Clear();
            OpenLocked(format);
            continue;
        }
        cv_.wait(lk, [&] { return stop_ || request_.Pending(); });
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
