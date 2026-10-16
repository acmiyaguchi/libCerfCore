#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_out_sink.h"

#include "host_wait_timer.h"

#include <cerfcore/core/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

namespace cerfcore {

class WaveOutSink::Impl {
public:
    using Clock = std::chrono::steady_clock;

    Impl() = default;
    ~Impl();
    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;

    void Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
               const char* log_tag);
    void Stop();
    void Post(UINT message, WPARAM wparam = 0, LPARAM lparam = 0) const;

    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                      uint16_t bits, bool allow_resampler, bool busy);
    bool IsOpen() const;
    bool SamplePosition(uint32_t& samples, uint32_t& generation) const;

    void Play(uint32_t slot, const uint8_t* bytes, uint32_t length);
    void Reset();

    static constexpr UINT kMsgCommand = WM_USER + 41;

private:
    struct SilentBlock {
        WAVEHDR*          hdr;
        Clock::time_point due;
    };

    struct OpenRequest {
        WAVEFORMATEX fmt;
        DWORD        flags;
    };

    static constexpr uint32_t kSilentQueue = 8;
    static constexpr WPARAM   kSilentDone  = 1;
    static_assert(kSlots <= kSilentQueue,
                  "every busy slot can hold one silent-mode completion");

    void ThreadMain(StartHandler on_start, CommandHandler on_command, DoneHandler on_done);
    void OnDone(WAVEHDR* hdr, const DoneHandler& on_done);
    void PlayHeader(WAVEHDR* hdr);
    void Unprepare(WAVEHDR* hdr);
    void ArmSilentTimer(Clock::duration delay);
    bool FormatMatchesLocked() const;
    OpenRequest BeginOpenLocked();
    MMRESULT OpenDevice(const OpenRequest& req, HWAVEOUT& device) const;
    bool FinishOpenLocked(const OpenRequest& req, MMRESULT r, HWAVEOUT device);
    bool OpenLocked();
    void CloseDeviceLocked();
    void LoseDeviceLocked(const char* call, MMRESULT result);
    void RetryOpen();
    Clock::duration PeriodForLocked(uint32_t length) const;
    void QueueSilentLocked(WAVEHDR* hdr);
    void DeliverDueSilent(const DoneHandler& on_done);
    void CancelSilentLocked();

    static constexpr UINT kMsgRetryOpen = WM_USER + 907;
    static constexpr Clock::duration kReopenInterval = std::chrono::seconds(1);

    mutable std::mutex mtx_;
    WAVEHDR            headers_[kSlots] = {};
    SilentBlock        silent_[kSilentQueue] = {};
    uint32_t           silent_count_ = 0;
    Clock::time_point  silent_tail_{};
    host_platform::HostWaitTimer silent_timer_;
    uint32_t           req_rate_     = 0;
    uint16_t           req_channels_ = 0;
    uint16_t           req_bits_     = 0;
    bool               req_allow_resampler_ = false;
    Clock::time_point  last_attempt_{};
    MMRESULT           last_fail_    = MMSYSERR_NOERROR;
    bool               retry_posted_ = false;
    uint32_t           generation_   = 0;

    DWORD             thread_id_   = 0;
    HANDLE            ready_event_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> shutdown_{false};

    HWAVEOUT    out_device_     = nullptr;
    uint32_t    open_rate_      = 0;
    uint16_t    open_channels_  = 0;
    uint16_t    open_bits_      = 0;
    const char* log_tag_        = "WaveOutSink";
};

WaveOutSink::Impl::~Impl() {
    Stop();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        CloseDeviceLocked();
    }
    if (ready_event_) CloseHandle(ready_event_);
}

void WaveOutSink::Impl::Start(StartHandler on_start, CommandHandler on_command,
                              DoneHandler on_done, const char* log_tag) {
    log_tag_     = log_tag;
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_      = std::thread([this, on_start, on_command, on_done] {
        ThreadMain(on_start, on_command, on_done);
    });
    WaitForSingleObject(ready_event_, INFINITE);
}

void WaveOutSink::Impl::Stop() {
    shutdown_.store(true, std::memory_order_release);
    if (thread_id_) PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void WaveOutSink::Impl::Post(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (thread_id_) PostThreadMessageW(thread_id_, message, wparam, lparam);
}

void WaveOutSink::Impl::ThreadMain(StartHandler on_start, CommandHandler on_command,
                                   DoneHandler on_done) {
    thread_id_ = GetCurrentThreadId();
    if (!silent_timer_.Create()) {
        LOG(Caution, "%s: CreateWaitableTimer failed; block pacing has no clock\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    HANDLE silent_timer = static_cast<HANDLE>(silent_timer_.NativeHandle());

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_event_);

    if (on_start) on_start();

    while (!shutdown_.load(std::memory_order_acquire)) {
        const DWORD w = MsgWaitForMultipleObjectsEx(1, &silent_timer, INFINITE,
                                                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (w == WAIT_OBJECT_0) {
            DeliverDueSilent(on_done);
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return;
            if (msg.message == kMsgRetryOpen) {
                RetryOpen();
                continue;
            }
            if (msg.message == kMsgCommand) {
                if (on_command) on_command(static_cast<uint32_t>(msg.wParam));
                continue;
            }
            if (msg.message == MM_WOM_DONE)
                OnDone(reinterpret_cast<WAVEHDR*>(msg.lParam), on_done);
        }
    }
}

void WaveOutSink::Impl::OnDone(WAVEHDR* hdr, const DoneHandler& on_done) {
    Unprepare(hdr);
    uint32_t slot = kNoSlot;
    for (uint32_t i = 0; i < kSlots; ++i) {
        if (hdr == &headers_[i]) {
            slot = i;
            break;
        }
    }
    if (on_done) on_done(slot);
}

void WaveOutSink::Impl::ArmSilentTimer(Clock::duration delay) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();
    if (!silent_timer_.Arm(ns)) {
        LOG(Caution, "%s: SetWaitableTimer failed; a block would never be reported\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
}

WaveOutSink::Impl::Clock::duration WaveOutSink::Impl::PeriodForLocked(uint32_t length) const {
    const uint64_t bytes_per_sec =
        static_cast<uint64_t>(req_rate_) * static_cast<uint32_t>(req_channels_) *
        (static_cast<uint32_t>(req_bits_) / 8u);
    if (bytes_per_sec == 0u) return Clock::duration::zero();
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(static_cast<double>(length) /
                                      static_cast<double>(bytes_per_sec)));
}

void WaveOutSink::Impl::QueueSilentLocked(WAVEHDR* hdr) {
    if (!hdr) {
        LOG(Caution, "%s: silent completion for a null header\n", log_tag_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    for (uint32_t i = 0; i < silent_count_; ++i) {
        if (silent_[i].hdr != hdr) continue;
        LOG(Caution, "%s: header %p is already awaiting a silent completion\n",
            log_tag_, static_cast<void*>(hdr));
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    if (silent_count_ == kSilentQueue) {
        LOG(Caution, "%s: %u silent blocks already pending\n", log_tag_, kSilentQueue);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const auto period = PeriodForLocked(hdr->dwBufferLength);
    if (period == Clock::duration::zero()) {
        LOG(Caution, "%s: cannot pace a silent block at %u Hz x %u ch x %u bit\n",
            log_tag_, req_rate_, req_channels_, req_bits_);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const auto now = Clock::now();
    silent_tail_ = std::max(now, silent_tail_) + period;
    silent_[silent_count_++] = SilentBlock{ hdr, silent_tail_ };
    if (silent_count_ == 1) ArmSilentTimer(silent_[0].due - now);
}

void WaveOutSink::Impl::DeliverDueSilent(const DoneHandler& on_done) {
    WAVEHDR* done[kSilentQueue] = {};
    uint32_t n = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const auto now = Clock::now();
        while (n < silent_count_ && silent_[n].due <= now) {
            done[n] = silent_[n].hdr;
            ++n;
        }
        silent_count_ -= n;
        for (uint32_t i = 0; i < silent_count_; ++i) silent_[i] = silent_[i + n];
        if (silent_count_ != 0) ArmSilentTimer(silent_[0].due - now);
    }
    for (uint32_t i = 0; i < n; ++i) OnDone(done[i], on_done);
}

void WaveOutSink::Impl::CancelSilentLocked() {
    silent_timer_.Cancel();
    for (uint32_t i = 0; i < silent_count_; ++i)
        Post(MM_WOM_DONE, kSilentDone, reinterpret_cast<LPARAM>(silent_[i].hdr));
    silent_count_ = 0;
    silent_tail_  = Clock::time_point{};
}

void WaveOutSink::Impl::CloseDeviceLocked() {
    if (out_device_) {
        waveOutReset(out_device_);
        const MMRESULT r = waveOutClose(out_device_);
        if (r != MMSYSERR_NOERROR) {
            LOG(Caution, "%s: waveOutClose failed mmresult=%u - device handle leaked\n",
                log_tag_, r);
        }
        out_device_ = nullptr;
    }
}

void WaveOutSink::Impl::LoseDeviceLocked(const char* call, MMRESULT result) {
    const MMRESULT reset = waveOutReset(out_device_);
    const MMRESULT close = waveOutClose(out_device_);
    LOG(Caution, "%s: host audio device lost (%s mmresult=%u, reset %u, close %u) "
            "- silent-mode (blocks paced from their own duration)\n",
            log_tag_, call, result, reset, close);
    out_device_   = nullptr;
    last_fail_    = result;
    last_attempt_ = Clock::now();
}

bool WaveOutSink::Impl::FormatMatchesLocked() const {
    return out_device_ != nullptr && open_rate_ == req_rate_ &&
           open_channels_ == req_channels_ && open_bits_ == req_bits_;
}

WaveOutSink::Impl::OpenRequest WaveOutSink::Impl::BeginOpenLocked() {
    last_attempt_ = Clock::now();
    OpenRequest req{};
    req.fmt.wFormatTag      = WAVE_FORMAT_PCM;
    req.fmt.nChannels       = req_channels_;
    req.fmt.nSamplesPerSec  = req_rate_;
    req.fmt.wBitsPerSample  = req_bits_;
    req.fmt.nBlockAlign     = static_cast<uint16_t>((req_bits_ / 8) * req_channels_);
    req.fmt.nAvgBytesPerSec = req_rate_ * req.fmt.nBlockAlign;
    req.fmt.cbSize          = 0;
    req.flags = CALLBACK_THREAD |
                (req_allow_resampler_ ? 0u : static_cast<DWORD>(WAVE_FORMAT_DIRECT));
    return req;
}

MMRESULT WaveOutSink::Impl::OpenDevice(const OpenRequest& req, HWAVEOUT& device) const {
    device = nullptr;
    return waveOutOpen(&device, WAVE_MAPPER, &req.fmt, thread_id_, 0, req.flags);
}

bool WaveOutSink::Impl::FinishOpenLocked(const OpenRequest& req, MMRESULT r, HWAVEOUT device) {
    const uint32_t rate     = req.fmt.nSamplesPerSec;
    const uint16_t channels = req.fmt.nChannels;
    const uint16_t bits     = req.fmt.wBitsPerSample;
    if (r != MMSYSERR_NOERROR) {
        if (r != last_fail_) {
            LOG(Caution, "%s: waveOutOpen(%u Hz x %u ch x %u bit) failed mmresult=%u "
                    "- silent-mode (blocks paced from their own duration)\n",
                    log_tag_, rate, channels, bits, r);
        }
        last_fail_ = r;
        return false;
    }
    out_device_    = device;
    open_rate_     = rate;
    open_channels_ = channels;
    open_bits_     = bits;
    last_fail_     = MMSYSERR_NOERROR;
    ++generation_;
    LOG(Periph, "[%s] waveOut opened %u Hz x %u ch x %u bit\n", log_tag_, rate, channels, bits);
    return true;
}

bool WaveOutSink::Impl::OpenLocked() {
    const OpenRequest req = BeginOpenLocked();
    HWAVEOUT device = nullptr;
    const MMRESULT r = OpenDevice(req, device);
    return FinishOpenLocked(req, r, device);
}

bool WaveOutSink::Impl::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                              uint16_t bits, bool allow_resampler, bool busy) {
    std::lock_guard<std::mutex> lk(mtx_);
    const bool changed = sample_rate_hz != req_rate_ || channels != req_channels_ ||
                         bits != req_bits_ || allow_resampler != req_allow_resampler_;
    req_rate_            = sample_rate_hz;
    req_channels_        = channels;
    req_bits_            = bits;
    req_allow_resampler_ = allow_resampler;
    if (FormatMatchesLocked()) return true;
    if (out_device_) {
        if (busy) return true;
        CloseDeviceLocked();
    }
    if (!changed && Clock::now() - last_attempt_ < kReopenInterval) return false;
    return OpenLocked();
}

void WaveOutSink::Impl::RetryOpen() {
    OpenRequest req{};
    {
        std::lock_guard<std::mutex> lk(mtx_);
        retry_posted_ = false;
        if (out_device_ || Clock::now() - last_attempt_ < kReopenInterval) return;
        req = BeginOpenLocked();
    }
    HWAVEOUT device = nullptr;
    const MMRESULT r = OpenDevice(req, device);
    std::lock_guard<std::mutex> lk(mtx_);
    FinishOpenLocked(req, r, device);
}

bool WaveOutSink::Impl::IsOpen() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return out_device_ != nullptr;
}

bool WaveOutSink::Impl::SamplePosition(uint32_t& samples, uint32_t& generation) const {
    std::lock_guard<std::mutex> lk(mtx_);
    generation = generation_;
    if (!out_device_) return false;
    MMTIME mmt{};
    mmt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(out_device_, &mmt, sizeof(mmt)) != MMSYSERR_NOERROR ||
        mmt.wType != TIME_SAMPLES)
        return false;
    samples = mmt.u.sample;
    return true;
}

void WaveOutSink::Impl::Play(uint32_t slot, const uint8_t* bytes, uint32_t length) {
    WAVEHDR* hdr = &headers_[slot];
    std::memset(hdr, 0, sizeof(*hdr));
    hdr->lpData         = reinterpret_cast<LPSTR>(const_cast<uint8_t*>(bytes));
    hdr->dwBufferLength = length;
    PlayHeader(hdr);
}

void WaveOutSink::Impl::PlayHeader(WAVEHDR* hdr) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_device_) {
        const char* call = "waveOutPrepareHeader";
        MMRESULT r = waveOutPrepareHeader(out_device_, hdr, sizeof(WAVEHDR));
        if (r == MMSYSERR_NOERROR) {
            call = "waveOutWrite";
            r = waveOutWrite(out_device_, hdr, sizeof(WAVEHDR));
            if (r == MMSYSERR_NOERROR) return;
            waveOutUnprepareHeader(out_device_, hdr, sizeof(WAVEHDR));
        }
        if (r != MMSYSERR_NODRIVER) {
            LOG(Caution, "%s: %s failed mmresult=%u\n", log_tag_, call, r);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        LoseDeviceLocked(call, r);
    }
    QueueSilentLocked(hdr);
    if (!retry_posted_ && Clock::now() - last_attempt_ >= kReopenInterval) {
        retry_posted_ = true;
        Post(kMsgRetryOpen);
    }
}

void WaveOutSink::Impl::Unprepare(WAVEHDR* hdr) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_device_ && hdr) waveOutUnprepareHeader(out_device_, hdr, sizeof(WAVEHDR));
}

void WaveOutSink::Impl::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_device_) waveOutReset(out_device_);
    CancelSilentLocked();
}

WaveOutSink::WaveOutSink() : impl_(std::make_unique<Impl>()) {}
WaveOutSink::~WaveOutSink() = default;

void WaveOutSink::Start(StartHandler on_start, CommandHandler on_command, DoneHandler on_done,
                        const char* log_tag) {
    impl_->Start(std::move(on_start), std::move(on_command), std::move(on_done), log_tag);
}
void WaveOutSink::Stop() { impl_->Stop(); }
void WaveOutSink::PostCommand(uint32_t command) { impl_->Post(Impl::kMsgCommand, command); }
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
