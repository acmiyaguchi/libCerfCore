#include "wave_out_sink.h"

#include "wave_out_callback_loop.h"
#include "wave_out_reopen_policy.h"

#include <cerfcore/core/log.h>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/AudioHardware.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace cerfcore {

namespace {

OSStatus DefaultOutputRateRanges(std::vector<AudioValueRange>& ranges) {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    AudioObjectID device = kAudioObjectUnknown;
    UInt32        size   = sizeof(device);
    OSStatus err = ::AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr,
                                                &size, &device);
    if (err != noErr) return err;
    address.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
    err = ::AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size);
    if (err != noErr) return err;
    ranges.resize(size / sizeof(AudioValueRange));
    err = ::AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, ranges.data());
    ranges.resize(size / sizeof(AudioValueRange));
    return err;
}

bool RateInRanges(uint32_t rate, const std::vector<AudioValueRange>& ranges) {
    for (const AudioValueRange& range : ranges)
        if (rate >= range.mMinimum && rate <= range.mMaximum) return true;
    return false;
}

}

class WaveOutSink::Impl final : private WaveOutDevice {
public:
    Impl() = default;
    ~Impl() { Stop(); }
    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    void Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
               const char* log_tag);
    void Stop();
    void PostCommand(uint32_t command) { callbacks_.PostCommand(command); }

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

    struct Stream {
        Impl*   owner;
        uint8_t silence;
    };

    static OSStatus Render(void* ref, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* stamp,
                           UInt32 bus, UInt32 frames, AudioBufferList* io);
    static OSStatus CreateUnit(const WaveOutFormat& format, Stream* stream, AudioUnit& unit,
                               const char*& step);
    void FillLocked(uint8_t* out, uint32_t bytes);
    void CompleteQueuedLocked();
    void RetryOpen();

    bool DeviceOpenLocked() const override { return unit_ != nullptr; }
    void CloseDeviceLocked(std::unique_lock<std::mutex>& lk) override;
    bool OpenDeviceLocked(std::unique_lock<std::mutex>& lk) override;

    mutable std::mutex  mtx_;
    std::deque<Block>   writes_;
    uint32_t            head_offset_ = 0;
    WaveOutCallbackLoop callbacks_;
    WaveOutReopenPolicy policy_;
    AudioUnit           unit_          = nullptr;
    Stream*             stream_        = nullptr;
    bool                shutdown_      = false;
    uint32_t            frame_bytes_   = 0;
    uint64_t            frames_played_ = 0;
    uint32_t            generation_    = 0;
    OSStatus            last_fail_     = noErr;
    const char*         log_tag_       = "WaveOutSink";
};

void WaveOutSink::Impl::Start(StartHandler on_start, CommandHandler on_command,
                              DoneHandler on_done, const char* log_tag) {
    log_tag_ = log_tag;
    callbacks_.Start(std::move(on_start), std::move(on_command), std::move(on_done),
                     [this] { RetryOpen(); });
}

void WaveOutSink::Impl::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        shutdown_ = true;
    }
    callbacks_.Stop();
    std::unique_lock<std::mutex> lk(mtx_);
    if (unit_ != nullptr) CloseDeviceLocked(lk);
}

OSStatus WaveOutSink::Impl::Render(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                                   UInt32, UInt32, AudioBufferList* io) {
    Stream* const               stream = static_cast<Stream*>(ref);
    Impl* const                 self   = stream->owner;
    std::lock_guard<std::mutex> lk(self->mtx_);
    for (UInt32 i = 0; i < io->mNumberBuffers; ++i) {
        uint8_t* const out   = static_cast<uint8_t*>(io->mBuffers[i].mData);
        const uint32_t bytes = io->mBuffers[i].mDataByteSize;
        if (self->stream_ == stream) {
            self->FillLocked(out, bytes);
        } else {
            std::memset(out, stream->silence, bytes);
        }
    }
    return noErr;
}

void WaveOutSink::Impl::FillLocked(uint8_t* out, uint32_t bytes) {
    uint32_t filled = 0;
    while (filled < bytes && !writes_.empty()) {
        const Block    block = writes_.front();
        const uint32_t n     = std::min(bytes - filled, block.length - head_offset_);
        std::memcpy(out + filled, block.bytes + head_offset_, n);
        filled += n;
        head_offset_ += n;
        if (head_offset_ == block.length) {
            writes_.pop_front();
            head_offset_ = 0;
            callbacks_.PostDone(block.slot);
        }
    }
    frames_played_ += filled / frame_bytes_;
    if (filled < bytes) std::memset(out + filled, stream_->silence, bytes - filled);
}

OSStatus WaveOutSink::Impl::CreateUnit(const WaveOutFormat& format, Stream* stream,
                                       AudioUnit& unit, const char*& step) {
    if (!format.allow_resampler) {
        std::vector<AudioValueRange> ranges;
        if (const OSStatus err = DefaultOutputRateRanges(ranges); err != noErr) {
            step = "AvailableNominalSampleRates";
            return err;
        }
        if (!RateInRanges(format.rate_hz, ranges)) {
            step = "native sample rate";
            return kAudioDeviceUnsupportedFormatError;
        }
    }
    AudioComponentDescription desc{};
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    const AudioComponent component = ::AudioComponentFindNext(nullptr, &desc);
    if (component == nullptr) {
        step = "AudioComponentFindNext";
        return kAudio_ParamError;
    }
    const uint32_t frame = static_cast<uint32_t>(format.channels) * (format.bits / 8u);
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = format.rate_hz;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = (format.bits == 16u ? kAudioFormatFlagIsSignedInteger : 0u) |
                            kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket   = frame;
    fmt.mFramesPerPacket  = 1u;
    fmt.mBytesPerFrame    = frame;
    fmt.mChannelsPerFrame = format.channels;
    fmt.mBitsPerChannel   = format.bits;
    AURenderCallbackStruct callback{&Render, stream};
    OSStatus               err = noErr;
    if ((err = ::AudioComponentInstanceNew(component, &unit)) != noErr) {
        step = "AudioComponentInstanceNew";
    } else if ((err = ::AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                                             kAudioUnitScope_Input, 0, &fmt, sizeof(fmt))) != noErr) {
        step = "StreamFormat";
    } else if ((err = ::AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
                                             kAudioUnitScope_Input, 0, &callback,
                                             sizeof(callback))) != noErr) {
        step = "SetRenderCallback";
    } else if ((err = ::AudioUnitInitialize(unit)) != noErr) {
        step = "AudioUnitInitialize";
    }
    return err;
}

bool WaveOutSink::Impl::OpenDeviceLocked(std::unique_lock<std::mutex>& lk) {
    policy_.NoteAttempt();
    const WaveOutFormat format = policy_.Requested();
    auto*       stream = new Stream{this, static_cast<uint8_t>(format.bits == 8u ? 0x80u : 0u)};
    AudioUnit   unit   = nullptr;
    OSStatus    err    = kAudio_ParamError;
    const char* step   = "format";
    lk.unlock();
    if ((format.bits == 8u || format.bits == 16u) && format.channels != 0u && format.rate_hz != 0u) {
        step = nullptr;
        err  = CreateUnit(format, stream, unit, step);
    }
    lk.lock();
    if (step == nullptr && !shutdown_) {
        unit_          = unit;
        stream_        = stream;
        frame_bytes_   = static_cast<uint32_t>(format.channels) * (format.bits / 8u);
        frames_played_ = 0;
        head_offset_   = 0;
        last_fail_     = noErr;
        policy_.NoteOpened();
        ++generation_;
        lk.unlock();
        err = ::AudioOutputUnitStart(unit);
        lk.lock();
        if (err == noErr) {
            LOG(Periph, "[%s] CoreAudio playback opened %u Hz x %u ch x %u bit\n", log_tag_,
                format.rate_hz, format.channels, format.bits);
            return true;
        }
        step    = "AudioOutputUnitStart";
        unit_   = nullptr;
        stream_ = nullptr;
        CompleteQueuedLocked();
    }
    lk.unlock();
    if (unit != nullptr) {
        ::AudioUnitUninitialize(unit);
        ::AudioComponentInstanceDispose(unit);
    }
    delete stream;
    lk.lock();
    if (step != nullptr && err != last_fail_) {
        LOG(Caution, "%s: CoreAudio playback open (%u Hz x %u ch x %u bit) failed at %s: OSStatus %d "
                     "- silent-mode (blocks paced from their own duration)\n",
            log_tag_, format.rate_hz, format.channels, format.bits, step, static_cast<int>(err));
    }
    if (step != nullptr) last_fail_ = err;
    return false;
}

void WaveOutSink::Impl::CompleteQueuedLocked() {
    for (const Block& b : writes_) callbacks_.PostDone(b.slot);
    writes_.clear();
    head_offset_ = 0;
}

void WaveOutSink::Impl::CloseDeviceLocked(std::unique_lock<std::mutex>& lk) {
    AudioUnit const unit   = unit_;
    Stream* const   stream = stream_;
    unit_                  = nullptr;
    stream_                = nullptr;
    CompleteQueuedLocked();
    lk.unlock();
    ::AudioOutputUnitStop(unit);
    ::AudioUnitUninitialize(unit);
    ::AudioComponentInstanceDispose(unit);
    delete stream;
    lk.lock();
}

bool WaveOutSink::Impl::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits,
                                     bool allow_resampler, bool busy) {
    std::unique_lock<std::mutex> lk(mtx_);
    return policy_.EnsureFormat(WaveOutFormat{sample_rate_hz, channels, bits, allow_resampler},
                                busy, *this, lk);
}

void WaveOutSink::Impl::RetryOpen() {
    std::unique_lock<std::mutex> lk(mtx_);
    if (policy_.BeginRetry(unit_ != nullptr) && !shutdown_) OpenDeviceLocked(lk);
}

bool WaveOutSink::Impl::IsOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return unit_ != nullptr;
}

bool WaveOutSink::Impl::SamplePosition(uint32_t& samples, uint32_t& generation) const {
    std::lock_guard<std::mutex> lk(mtx_);
    generation = generation_;
    if (unit_ == nullptr) return false;
    samples = static_cast<uint32_t>(frames_played_);
    return true;
}

void WaveOutSink::Impl::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (unit_ != nullptr) {
        writes_.push_back(Block{slot, bytes, length});
        return;
    }
    const WaveOutFormat& req = policy_.Requested();
    callbacks_.QueueSilent(slot, length, req.rate_hz, req.channels, req.bits, log_tag_);
    if (policy_.ShouldPostRetry()) callbacks_.PostRetryOpen();
}

void WaveOutSink::Impl::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (unit_ != nullptr) CompleteQueuedLocked();
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
