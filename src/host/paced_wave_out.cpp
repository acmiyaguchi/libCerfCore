#include "paced_wave_out.h"

#include <cerfcore/core/log.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace cerfcore {

void PacedWaveOut::Start(const char* log_tag, uint32_t rate_hz, uint16_t channels,
                         uint16_t bits, bool allow_resampler) {
    log_tag_         = log_tag;
    allow_resampler_ = allow_resampler;
    fmt_rate_.store(rate_hz, std::memory_order_release);
    fmt_channels_.store(channels, std::memory_order_release);
    fmt_bits_.store(bits, std::memory_order_release);
    sink_.Start(
        [this] {
            std::lock_guard<std::mutex> lk(audio_mutex_);
            ApplyFormatLocked();
        },
        [this](uint32_t command) { OnCommand(command); },
        [this](uint32_t slot) { OnBlockDone(slot); },
        log_tag);
}

void PacedWaveOut::Stop() { sink_.Stop(); }

void PacedWaveOut::SetFormat(uint32_t rate_hz, uint16_t channels, uint16_t bits) {
    {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        fmt_rate_.store(rate_hz, std::memory_order_release);
        fmt_channels_.store(channels, std::memory_order_release);
        fmt_bits_.store(bits, std::memory_order_release);
    }
    sink_.PostCommand(kCommandSetFormat);
}

uint64_t PacedWaveOut::RequestedFormatLocked() const {
    return static_cast<uint64_t>(fmt_rate_.load(std::memory_order_acquire)) |
           (static_cast<uint64_t>(fmt_channels_.load(std::memory_order_acquire)) << 32) |
           (static_cast<uint64_t>(fmt_bits_.load(std::memory_order_acquire)) << 48);
}

void PacedWaveOut::SwitchDeviceLocked(uint64_t format) {
    const uint32_t rate     = static_cast<uint32_t>(format);
    const uint16_t channels = static_cast<uint16_t>(format >> 32);
    const uint16_t bits     = static_cast<uint16_t>(format >> 48);
    sink_.EnsureFormat(rate, channels, bits, allow_resampler_, false);
    device_format_ = format;
    ++device_epoch_;
    position_valid_ = false;
    LOG(Periph, "[%s] device format %u Hz x %u ch x %u bit open=%d\n",
        log_tag_, rate, channels, bits, sink_.IsOpen() ? 1 : 0);
}

void PacedWaveOut::ApplyFormatLocked() {
    const uint64_t format = RequestedFormatLocked();
    if (static_cast<uint32_t>(format) == 0u) return;
    if (pending_count_ == 0 && outstanding_ == 0 && format != device_format_)
        SwitchDeviceLocked(format);
}

bool PacedWaveOut::HasFreeSlotLocked() const {
    for (uint32_t i = 0; i < kSlots; ++i)
        if (!slot_busy_[i] && slot_stale_[i] == 0u) return true;
    return false;
}

void PacedWaveOut::FlushPendingBlockLocked() {
    if (priming_) return;
    while (pending_count_ != 0 && HasFreeSlotLocked()) {
        const uint64_t format = pending_format_[pending_head_];
        if (format != device_format_) {
            if (outstanding_ != 0) return;
            SwitchDeviceLocked(format);
        }
        PlayLocked(pending_buffer_[pending_head_], pending_length_[pending_head_]);
        pending_head_ = (pending_head_ + 1u) % kPending;
        --pending_count_;
    }
}

void PacedWaveOut::PlayLocked(const uint8_t* bytes, uint32_t length) {
    uint32_t slot = kSlots;
    for (uint32_t i = 0; i < kSlots; ++i)
        if (!slot_busy_[i] && slot_stale_[i] == 0u) { slot = i; break; }
    if (slot == kSlots) {
        LOG(Caution, "[%s] no free slot with %u outstanding\n", log_tag_, outstanding_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    std::memcpy(buffers_[slot], bytes, length);
    slot_length_[slot] = length;

    sink_.Play(slot, buffers_[slot], length);
    slot_busy_[slot]    = true;
    ++outstanding_;
}

bool PacedWaveOut::ReleaseSlotLocked(uint32_t slot) {
    if (!slot_busy_[slot]) return false;
    slot_busy_[slot] = false;
    --outstanding_;
    return true;
}

void PacedWaveOut::OnCommand(uint32_t command) {
    if (command == kCommandSetFormat) {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        ApplyFormatLocked();
        FlushPendingBlockLocked();
        return;
    }
    if (command == kCommandFlushPending) {
        std::lock_guard<std::mutex> lk(audio_mutex_);
        FlushPendingBlockLocked();
        ApplyFormatLocked();
    }
}

void PacedWaveOut::OnBlockDone(uint32_t slot) {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    if (slot != WaveOutSink::kNoSlot) {
        if (slot_stale_[slot] != 0u) {
            --slot_stale_[slot];
        } else if (ReleaseSlotLocked(slot) && output_active_.load(std::memory_order_acquire)) {
            RefreshPositionLocked();
            if (device_started_ && !finished_ && outstanding_ == 0 && pending_count_ == 0)
                ++starved_;
            if (!device_started_) {
                device_started_ = true;
                rate_match_.DeviceStarted();
            }
        }
    }
    FlushPendingBlockLocked();
    ApplyFormatLocked();
}

void PacedWaveOut::BeginAudioOut() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    rate_match_.Restart();
    device_started_ = false;
    starved_        = 0;
    priming_        = outstanding_ == 0 && pending_count_ == 0;
    finished_       = false;
    output_active_.store(true, std::memory_order_release);
}

void PacedWaveOut::FinishAudioOut() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    finished_ = true;
    if (!priming_) return;
    priming_ = false;
    LOG(Periph, "[%s] stream ended before the set point, %u blocks queued\n",
        log_tag_, pending_count_);
    sink_.PostCommand(kCommandFlushPending);
}

void PacedWaveOut::StopAudioOut() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    output_active_.store(false, std::memory_order_release);
    device_started_ = false;
    priming_        = false;
    finished_       = false;
    ++device_epoch_;
    position_valid_ = false;
    pending_head_  = 0;
    pending_count_ = 0;
    sink_.Reset();
    for (uint32_t i = 0; i < kSlots; ++i) {
        if (slot_busy_[i]) ++slot_stale_[i];
        slot_busy_[i] = false;
    }
    outstanding_ = 0;
}

void PacedWaveOut::SpliceOverDroppedLocked(const uint8_t* dropped, uint32_t length) {
    constexpr uint32_t kFadeFrames = 128;
    const uint32_t chans  = fmt_channels_.load(std::memory_order_relaxed);
    const uint32_t frame  = chans * 2u;
    const uint32_t last   = (pending_head_ + pending_count_ - 1u) % kPending;
    if (pending_format_[last] != RequestedFormatLocked()) return;
    uint8_t*       tail   = pending_buffer_[last];
    const uint32_t frames = std::min({ pending_length_[last] / frame,
                                       length / frame, kFadeFrames });
    uint8_t*       a = tail + pending_length_[last] - frames * frame;
    const uint8_t* b = dropped + length - frames * frame;
    for (uint32_t f = 0; f < frames; ++f) {
        const int32_t w_b = static_cast<int32_t>(f + 1);
        const int32_t w_a = static_cast<int32_t>(frames) - static_cast<int32_t>(f) - 1;
        const int32_t sum = static_cast<int32_t>(frames);
        for (uint32_t c = 0; c < chans; ++c) {
            const uint32_t off = f * frame + c * 2u;
            int16_t sa, sb;
            std::memcpy(&sa, a + off, 2);
            std::memcpy(&sb, b + off, 2);
            const int16_t mix = static_cast<int16_t>((sa * w_a + sb * w_b) / sum);
            std::memcpy(a + off, &mix, 2);
        }
    }
}

uint32_t PacedWaveOut::QueuedBytesLocked() const {
    uint32_t bytes = 0;
    for (uint32_t i = 0; i < kSlots; ++i)
        if (slot_busy_[i]) bytes += slot_length_[i];
    for (uint32_t i = 0; i < pending_count_; ++i)
        bytes += pending_length_[(pending_head_ + i) % kPending];
    return bytes;
}

uint32_t PacedWaveOut::RateMatchLocked(const uint8_t* bytes, uint32_t length) {
    const uint32_t bits  = fmt_bits_.load(std::memory_order_relaxed);
    const uint32_t chans = fmt_channels_.load(std::memory_order_relaxed);
    if (bits != 16 || chans == 0 || chans > DynamicRateResampler::kMaxChannels) {
        LOG(Caution, "[%s] rate match on an unmodeled stream: %u ch x %u bit\n",
            log_tag_, chans, bits);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t frame     = chans * 2u;
    const uint32_t in_frames = length / frame;
    std::memcpy(rate_in_, bytes, static_cast<size_t>(in_frames) * frame);
    const double capacity = static_cast<double>(kSlots + kPending) * in_frames;
    const double fill     = static_cast<double>(QueuedBytesLocked()) / frame;
    /* Arntzen, "Dynamic Rate Control for Retro Game Emulators", §2.3: eq. (2) decreases
       the ratio over half full and increases it below half full. */
    set_point_frames_ = capacity / 2.0;
    if (rate_match_.CountBlock()) rate_match_.Sample(DevicePositionLocked());
    const uint32_t produced = rate_match_.Process(rate_in_, in_frames, chans, fill, capacity,
                                                  rate_out_, kMaxBlock / frame);
    if (produced == UINT32_MAX) {
        LOG(Caution, "[%s] rate match output of a %u-frame block overflows %u frames\n",
            log_tag_, in_frames, kMaxBlock / frame);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    double ratio = 0.0;
    if (rate_match_.TakeEstimate(&ratio)) {
        LOG(Periph, "[%s] rate match %.5f, queue %.0f of %.0f frames, starved %u, unfed %u\n",
            log_tag_, ratio, fill, capacity, starved_, unfed_total_);
        starved_ = 0;
    }
    return produced * frame;
}

DevicePosition PacedWaveOut::DevicePositionLocked() const {
    DevicePosition p;
    p.valid   = position_valid_;
    p.samples = position_;
    p.wall_s  = position_wall_s_;
    p.epoch   = device_epoch_;
    p.unfed   = position_unfed_;
    p.rate_hz = static_cast<uint32_t>(device_format_);
    return p;
}

void PacedWaveOut::RefreshPositionLocked() {
    if (pending_count_ == 0) ++unfed_total_;
    position_unfed_ = unfed_total_;
    position_valid_ = false;
    uint32_t samples    = 0;
    uint32_t generation = 0;
    const bool sampled  = sink_.SamplePosition(samples, generation);
    if (generation != sink_generation_) {
        sink_generation_ = generation;
        ++device_epoch_;
    }
    if (!sampled) return;
    position_        = samples;
    position_wall_s_ = std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch()).count();
    position_valid_  = true;
}

void PacedWaveOut::QueueOutputInHostBlocks(const void* host_bytes, uint32_t length) {
    const uint32_t rate  = fmt_rate_.load(std::memory_order_acquire);
    const uint32_t frame = static_cast<uint32_t>(fmt_channels_.load(std::memory_order_acquire)) *
                           (fmt_bits_.load(std::memory_order_acquire) / 8u);
    if (rate == 0u || frame == 0u || length % frame != 0u) {
        LOG(Caution, "[%s] %u bytes queued in host blocks of a %u Hz, %u-byte-frame format\n",
            log_tag_, length, rate, frame);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t block_frames = HostBlockFrames();
    const uint32_t frames = length / frame;
    const uint32_t blocks = (frames + block_frames - 1u) / block_frames;
    const uint8_t* bytes  = static_cast<const uint8_t*>(host_bytes);
    uint32_t done = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        const uint32_t n = (frames - done) / (blocks - i);
        QueueOutput(bytes + static_cast<size_t>(done) * frame, n * frame);
        done += n;
    }
}

uint32_t PacedWaveOut::HostBlockFrames() const {
    const uint64_t rate = fmt_rate_.load(std::memory_order_acquire);
    return std::max<uint32_t>(1u, static_cast<uint32_t>(rate * kHostBlockFrames / kHostBlockRateHz));
}

bool PacedWaveOut::QueueOutput(const void* host_bytes, uint32_t length) {
    if (length == 0) return false;
    if (length > kMaxBlock) length = kMaxBlock;

    std::lock_guard<std::mutex> lk(audio_mutex_);
    if (!output_active_.load(std::memory_order_acquire)) return false;

    length     = RateMatchLocked(static_cast<const uint8_t*>(host_bytes), length);
    host_bytes = rate_out_;
    if (length == 0) return true;

    const uint64_t format = RequestedFormatLocked();
    if (priming_ || format != device_format_ || pending_count_ != 0 || !HasFreeSlotLocked()) {
        if (pending_count_ == kPending) {
            SpliceOverDroppedLocked(static_cast<const uint8_t*>(host_bytes), length);
            LOG(Periph, "[%s] dropped a %u-byte block: pending stash full (%u) with %u outstanding\n",
                log_tag_, length, pending_count_, outstanding_);
            return false;
        }
        const uint32_t slot = (pending_head_ + pending_count_) % kPending;
        std::memcpy(pending_buffer_[slot], host_bytes, length);
        pending_length_[slot] = length;
        pending_format_[slot] = format;
        ++pending_count_;
        if (format != device_format_) sink_.PostCommand(kCommandSetFormat);
        const uint32_t frame = fmt_channels_.load(std::memory_order_relaxed) * 2u;
        if (priming_ && static_cast<double>(QueuedBytesLocked()) / frame >= set_point_frames_) {
            priming_ = false;
            LOG(Periph, "[%s] device starts at the set point, %u blocks queued\n",
                log_tag_, pending_count_);
            sink_.PostCommand(kCommandFlushPending);
        }
        return true;
    }

    ++unfed_total_;
    PlayLocked(static_cast<const uint8_t*>(host_bytes), length);
    return true;
}

}
