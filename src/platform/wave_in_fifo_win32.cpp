#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include "wave_in_fifo.h"

#include <cerfcore/core/log.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr UINT kMsgOpen = WM_USER + 0x10u;

class WaveInSink {
public:
    using ThreadCallback = std::function<void()>;
    using MessageHandler = std::function<void(const MSG&)>;

    WaveInSink() = default;
    ~WaveInSink();
    WaveInSink(const WaveInSink&)            = delete;
    WaveInSink& operator=(const WaveInSink&) = delete;

    void Start(ThreadCallback on_start, MessageHandler on_message,
               const char* log_tag);
    void Stop();

    void Post(UINT message, WPARAM wparam = 0, LPARAM lparam = 0) const;

    bool EnsureFormat(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);

    void Requeue(WAVEHDR* hdr);

private:
    static constexpr uint32_t kRecordBuffers  = 8u;
    static constexpr uint32_t kRecordBufBytes = 4096u;

    void ThreadMain(ThreadCallback on_start, MessageHandler on_message);
    void CloseDevice();

    DWORD             thread_id_   = 0;
    HANDLE            ready_event_ = nullptr;
    std::thread       thread_;
    std::atomic<bool> shutdown_{false};

    HWAVEIN  in_device_     = nullptr;
    uint32_t open_rate_     = 0;
    uint16_t open_channels_ = 0;
    uint16_t open_bits_     = 0;

    struct RecBuf {
        WAVEHDR              hdr{};
        std::vector<uint8_t> bytes;
    };
    RecBuf      bufs_[kRecordBuffers];
    const char* log_tag_ = "WaveInSink";
};

WaveInSink::~WaveInSink() {
    Stop();
    CloseDevice();
    if (ready_event_) CloseHandle(ready_event_);
}

void WaveInSink::Start(ThreadCallback on_start, MessageHandler on_message,
                       const char* log_tag) {
    log_tag_     = log_tag;
    ready_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_      = std::thread([this, on_start, on_message] {
        ThreadMain(on_start, on_message);
    });
    WaitForSingleObject(ready_event_, INFINITE);
}

void WaveInSink::Stop() {
    shutdown_.store(true, std::memory_order_release);
    if (thread_id_) PostThreadMessageW(thread_id_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void WaveInSink::Post(UINT message, WPARAM wparam, LPARAM lparam) const {
    if (thread_id_) PostThreadMessageW(thread_id_, message, wparam, lparam);
}

void WaveInSink::ThreadMain(ThreadCallback on_start, MessageHandler on_message) {
    thread_id_ = GetCurrentThreadId();

    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready_event_);

    if (on_start) on_start();

    while (!shutdown_.load(std::memory_order_acquire) &&
           GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (on_message) on_message(msg);
    }
}

void WaveInSink::CloseDevice() {
    if (!in_device_) return;
    waveInReset(in_device_);
    for (auto& b : bufs_) {
        if (b.hdr.dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(in_device_, &b.hdr, sizeof(WAVEHDR));
    }
    waveInClose(in_device_);
    in_device_ = nullptr;
}

bool WaveInSink::EnsureFormat(uint32_t sample_rate_hz, uint16_t channels,
                              uint16_t bits) {
    if (in_device_ && open_rate_ == sample_rate_hz &&
        open_channels_ == channels && open_bits_ == bits) {
        return true;
    }
    if (in_device_) CloseDevice();

    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = channels;
    fmt.nSamplesPerSec  = sample_rate_hz;
    fmt.wBitsPerSample  = bits;
    fmt.nBlockAlign     = static_cast<uint16_t>((bits / 8) * channels);
    fmt.nAvgBytesPerSec = sample_rate_hz * fmt.nBlockAlign;
    fmt.cbSize          = 0;

    MMRESULT r = waveInOpen(&in_device_, WAVE_MAPPER, &fmt,
                            thread_id_, 0, CALLBACK_THREAD);
    if (r != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveInOpen(%u Hz x %u ch x %u bit) failed mmresult=%u "
                "- microphone capture unavailable\n",
                log_tag_, sample_rate_hz, channels, bits, r);
        in_device_ = nullptr;
        return false;
    }

    for (auto& b : bufs_) {
        b.bytes.assign(kRecordBufBytes, 0);
        b.hdr = WAVEHDR{};
        b.hdr.lpData         = reinterpret_cast<LPSTR>(b.bytes.data());
        b.hdr.dwBufferLength = kRecordBufBytes;
        if (waveInPrepareHeader(in_device_, &b.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
            waveInAddBuffer(in_device_, &b.hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            LOG(Caution, "%s: record buffer prime failed\n", log_tag_);
            CloseDevice();
            return false;
        }
    }

    if (waveInStart(in_device_) != MMSYSERR_NOERROR) {
        LOG(Caution, "%s: waveInStart failed\n", log_tag_);
        CloseDevice();
        return false;
    }

    open_rate_     = sample_rate_hz;
    open_channels_ = channels;
    open_bits_     = bits;
    LOG(Periph, "[%s] waveIn opened %u Hz x %u ch x %u bit\n",
        log_tag_, sample_rate_hz, channels, bits);
    return true;
}

void WaveInSink::Requeue(WAVEHDR* hdr) {
    if (!in_device_ || !hdr) return;
    hdr->dwBytesRecorded = 0;
    hdr->dwFlags &= ~WHDR_DONE;
    waveInAddBuffer(in_device_, hdr, sizeof(WAVEHDR));
}

}

class WaveInFifo::Impl {
public:
    void Start(const char* log_tag, size_t cap_bytes);
    void Stop();

    void   Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);
    void   Clear();
    size_t Take(uint8_t* dst, size_t bytes);

private:
    static uint64_t FormatKey(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits);

    void OnThreadMessage(const MSG& msg);
    void OpenOnThread();
    void OnRecordedData(const uint8_t* data, uint32_t bytes);

    WaveInSink            sink_;
    std::mutex            mtx_;
    std::deque<uint8_t>   fifo_;
    std::atomic<uint64_t> requested_{0};
    uint64_t              open_      = 0;
    size_t                cap_bytes_ = 0;
    const char*           log_tag_   = "WaveInFifo";
};

uint64_t WaveInFifo::Impl::FormatKey(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    return static_cast<uint64_t>(sample_rate_hz) | (static_cast<uint64_t>(channels) << 32) |
           (static_cast<uint64_t>(bits) << 48);
}

void WaveInFifo::Impl::Start(const char* log_tag, size_t cap_bytes) {
    log_tag_   = log_tag;
    cap_bytes_ = cap_bytes;
    sink_.Start(nullptr, [this](const MSG& msg) { OnThreadMessage(msg); }, log_tag);
}

void WaveInFifo::Impl::Stop() { sink_.Stop(); }

void WaveInFifo::Impl::Request(uint32_t sample_rate_hz, uint16_t channels, uint16_t bits) {
    const uint64_t key = FormatKey(sample_rate_hz, channels, bits);
    if (requested_.exchange(key) != key) sink_.Post(kMsgOpen);
}

void WaveInFifo::Impl::Clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    fifo_.clear();
}

size_t WaveInFifo::Impl::Take(uint8_t* dst, size_t bytes) {
    size_t taken = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        taken = std::min(bytes, fifo_.size());
        std::copy(fifo_.begin(), fifo_.begin() + taken, dst);
        fifo_.erase(fifo_.begin(), fifo_.begin() + taken);
    }
    std::fill(dst + taken, dst + bytes, uint8_t{0});
    return taken;
}

void WaveInFifo::Impl::OnThreadMessage(const MSG& msg) {
    if (msg.message == kMsgOpen) {
        OpenOnThread();
    } else if (msg.message == MM_WIM_DATA) {
        auto* hdr = reinterpret_cast<LPWAVEHDR>(msg.lParam);
        if (!hdr) return;
        OnRecordedData(reinterpret_cast<const uint8_t*>(hdr->lpData), hdr->dwBytesRecorded);
        sink_.Requeue(hdr);
    }
}

void WaveInFifo::Impl::OpenOnThread() {
    const uint64_t key = requested_.load();
    if (key == open_) return;
    open_ = key;
    Clear();
    const uint32_t rate     = static_cast<uint32_t>(key);
    const uint16_t channels = static_cast<uint16_t>(key >> 32);
    const uint16_t bits     = static_cast<uint16_t>(key >> 48);
    if (!sink_.EnsureFormat(rate, channels, bits)) {
        LOG(Caution, "[%s] microphone capture unavailable at %u Hz (host waveIn open failed); "
                     "the guest records silence\n", log_tag_, rate);
    }
}

void WaveInFifo::Impl::OnRecordedData(const uint8_t* data, uint32_t bytes) {
    if (bytes == 0u) return;
    std::lock_guard<std::mutex> lk(mtx_);
    fifo_.insert(fifo_.end(), data, data + bytes);
    if (fifo_.size() > cap_bytes_) fifo_.erase(fifo_.begin(), fifo_.end() - cap_bytes_);
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
