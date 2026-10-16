#include "host_serial_forward.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/user_alert.h>
#include "../../platform/host_clock.h"
#include <cerfcore/state/emulation_freeze.h>

#include <utility>

namespace cerfcore {

namespace {
constexpr uint32_t kReadTimeoutMs  = 50;
constexpr uint32_t kWriteTimeoutMs = 2000;

host_platform::SerialPortSettings ToPortSettings(const SerialLine::LineConfig& c,
                                                 bool dtr, bool rts) {
    using Settings = host_platform::SerialPortSettings;
    Settings s;
    s.baud      = c.baud;
    s.data_bits = c.data_bits;
    switch (c.parity) {
        case SerialLine::LineConfig::Parity::None:  s.parity = Settings::Parity::kNone;  break;
        case SerialLine::LineConfig::Parity::Odd:   s.parity = Settings::Parity::kOdd;   break;
        case SerialLine::LineConfig::Parity::Even:  s.parity = Settings::Parity::kEven;  break;
        case SerialLine::LineConfig::Parity::Mark:  s.parity = Settings::Parity::kMark;  break;
        case SerialLine::LineConfig::Parity::Space: s.parity = Settings::Parity::kSpace; break;
    }
    switch (c.stop) {
        case SerialLine::LineConfig::Stop::One:          s.stop = Settings::StopBits::kOne;          break;
        case SerialLine::LineConfig::Stop::OnePointFive: s.stop = Settings::StopBits::kOnePointFive; break;
        case SerialLine::LineConfig::Stop::Two:          s.stop = Settings::StopBits::kTwo;          break;
    }
    s.dtr = dtr;
    s.rts = rts;
    return s;
}
}

HostSerialForward::HostSerialForward(std::string host_port, CerfEmulator& emu)
    : emu_(emu), port_name_(std::move(host_port)) {}

HostSerialForward::~HostSerialForward() { OnClose(); }

void HostSerialForward::ReportDead(const std::string& detail) {
    if (dead_.exchange(true)) return;
    emu_.Get<UserAlert>().Post(
        AlertLevel::kWarning, "Serial port forwarder - CE Runtime Foundation",
        "The serial port forwarder lost host " + port_name_ + ".\n\n" + detail +
            "\n\nThe port has been ejected. Check that the port exists "
            "and is not in use by another application, then insert it "
            "again.",
        on_dead_);
}

bool HostSerialForward::WithGuestAccess(const std::function<void()>& touch) {
    auto& freeze = emu_.Get<EmulationFreeze>();
    while (running_.load()) {
        auto frozen = freeze.TryWorkerSection();
        if (frozen.owns_lock()) {
            touch();
            return true;
        }
        if (port_.WaitStopped(1)) return false;
    }
    return false;
}

void HostSerialForward::OnOpen() {
    uint32_t err = 0;
    if (!port_.Open(port_name_, err)) {
        LOG(Caution, "[SerialFwd] cannot open host %s (err %u)\n", port_name_.c_str(), err);
        if (uart_) uart_->SetModemInputs(false, false, false, false);
        ReportDead("The port could not be opened (error " + std::to_string(err) + ").");
        return;
    }

    ApplyLineConfig(uart_ ? uart_->GetLineConfig() : SerialLine::LineConfig{});

    last_ms_.store(kModemLinesUnpushed);

    port_.StartIo(kReadTimeoutMs, kWriteTimeoutMs);

    if (uart_)
        uart_->SetLineConfigCallback(
            [this](const SerialLine::LineConfig& c) { ApplyLineConfig(c); });

    running_ = true;
    reader_  = std::thread([this] { ReaderLoop(); });
    writer_  = std::thread([this] { WriterLoop(); });
    LOG(Periph, "[SerialFwd] bridging guest COM <-> host %s\n", port_name_.c_str());
}

void HostSerialForward::OnClose() {
    if (uart_) uart_->SetLineConfigCallback(nullptr);
    StopThreads();
    if (uart_) uart_->SetModemInputs(false, false, false, false);
    port_.Close();
}

void HostSerialForward::StopThreads() {
    running_.store(false);
    port_.RequestStop();
    tx_cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    if (reader_.joinable()) reader_.join();
}

void HostSerialForward::ApplyLineConfig(const SerialLine::LineConfig& c) {
    std::lock_guard<std::mutex> lk(ctl_mu_);
    if (!port_.IsOpen()) return;
    uint32_t err = 0;
    const host_platform::SerialConfigureResult result =
        port_.Configure(ToPortSettings(c, dtr_, rts_), err);
    if (result == host_platform::SerialConfigureResult::kStateUnreadable) return;
    if (result == host_platform::SerialConfigureResult::kRejected) {
        LOG(Caution, "[SerialFwd] %s line configuration failed (err %u): the port did not "
            "take baud=%u bits=%u parity=%u stop=%u DTR=%d RTS=%d\n",
            port_name_.c_str(), err, c.baud, c.data_bits,
            (unsigned)c.parity, (unsigned)c.stop, (int)dtr_, (int)rts_);
        return;
    }
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] line cfg %s baud=%u bits=%u parity=%u stop=%u "
        "DTR=%d RTS=%d\n", port_name_.c_str(), c.baud, c.data_bits,
        (unsigned)c.parity, (unsigned)c.stop, (int)dtr_, (int)rts_);
#endif
}

void HostSerialForward::OnGuestTx(const uint8_t* data, size_t n) {
    if (!port_.IsOpen() || n == 0) return;
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] TX guest->host %s n=%zu b0=0x%02X\n",
        port_name_.c_str(), n, data[0]);
#endif
    {
        std::lock_guard<std::mutex> lk(tx_mu_);
        tx_buf_.insert(tx_buf_.end(), data, data + n);
    }
    tx_cv_.notify_one();
}

void HostSerialForward::ResendModemInputs() { last_ms_.store(kModemLinesUnpushed); }

void HostSerialForward::OnControlLines(bool dtr, bool rts) {
    std::lock_guard<std::mutex> lk(ctl_mu_);
    dtr_ = dtr;
    rts_ = rts;
    if (!port_.IsOpen()) return;
#if CERF_DEV_MODE
    LOG(Periph, "[SerialFwd] drive host %s DTR=%d RTS=%d\n",
        port_name_.c_str(), dtr, rts);
#endif
    port_.SetControlLines(dtr, rts);
}

void HostSerialForward::ReaderLoop() {
    uint8_t buf[512];
    while (running_.load()) {
        uint8_t cur = 0;
        if (port_.ModemLines(cur)) {
            if (cur != last_ms_.load()) {
                const bool cts = (cur & host_platform::kModemCts)  != 0;
                const bool dsr = (cur & host_platform::kModemDsr)  != 0;
                const bool ri  = (cur & host_platform::kModemRing) != 0;
                const bool dcd = (cur & host_platform::kModemDcd)  != 0;
#if CERF_DEV_MODE
                LOG(Periph, "[SerialFwd] host modem CTS=%d DSR=%d RI=%d DCD=%d\n",
                    cts, dsr, ri, dcd);
#endif
                if (uart_) {
                    if (!WithGuestAccess([&] {
                            uart_->SetModemInputs(cts, dsr, ri, dcd);
                        }))
                        break;
                }
                last_ms_.store(cur);
            }
        }

        uint32_t got = 0;
        uint32_t err = 0;
        const host_platform::SerialIoResult result = port_.Read(buf, sizeof buf, got, err);
        if (result == host_platform::SerialIoResult::kFailed) {
            LOG(Caution, "[SerialFwd] %s reader err=%u; host->guest "
                "bridge is dead\n", port_name_.c_str(), err);
            ReportDead("Reading from the port failed (error " + std::to_string(err) + ").");
            break;
        }
        if (result == host_platform::SerialIoResult::kStopped) break;
        if (got > 0 && uart_) {
#if CERF_DEV_MODE
            LOG(Periph, "[SerialFwd] RX host->guest %s n=%u b0=0x%02X\n",
                port_name_.c_str(), got, buf[0]);
#endif
            if (!WithGuestAccess([&] { uart_->PushRx(buf, got); })) break;
        }
    }
}

void HostSerialForward::WriterLoop() {
    const int64_t freq = host_platform::MonotonicFrequency();
    int64_t next = host_platform::MonotonicTicks();
    std::vector<uint8_t> local;
    while (true) {
        {
            std::unique_lock<std::mutex> lk(tx_mu_);
            tx_cv_.wait(lk, [this] { return !tx_buf_.empty() || !running_.load(); });
            if (!running_.load() && tx_buf_.empty()) return;
            local.swap(tx_buf_);
        }

        const SerialLine::LineConfig cfg =
            uart_ ? uart_->GetLineConfig() : SerialLine::LineConfig{};
        const int64_t byte_ticks =
            cfg.baud ? (int64_t)(cfg.BitsPerChar() * (double)freq / cfg.baud) : 0;

        for (size_t i = 0; i < local.size() && running_.load(); ++i) {
            const int64_t now = host_platform::MonotonicTicks();
            if (next < now) next = now;
            if (next > now && !port_.PaceWait(next - now)) {
#if CERF_DEV_MODE
                LOG(Periph, "[SerialFwd] writer pace-wait aborted at byte %zu/%zu\n",
                    i, local.size());
#endif
                break;
            }

            uint32_t wrote = 0;
            uint32_t err   = 0;
            const host_platform::SerialIoResult result = port_.WriteByte(local[i], wrote, err);
            if (result == host_platform::SerialIoResult::kStopped) {
#if CERF_DEV_MODE
                LOG(Periph, "[SerialFwd] writer wait aborted at byte %zu/%zu; "
                    "thread exits\n", i, local.size());
#endif
                return;
            }
            if (result == host_platform::SerialIoResult::kFailed) {
                LOG(Caution, "[SerialFwd] %s writer err=%u at byte "
                    "%zu/%zu; guest->host bytes dropped\n", port_name_.c_str(),
                    err, i, local.size());
                ReportDead("Writing to the port failed (error " + std::to_string(err) + ").");
                break;
            }
            if (wrote == 0) {
                LOG(Caution, "[SerialFwd] %s writer timed out at byte %zu/%zu; "
                    "guest->host bytes dropped\n", port_name_.c_str(), i,
                    local.size());
                ReportDead("The port stopped accepting data (write timed out).");
                break;
            }
            next += byte_ticks;
        }
        local.clear();
    }
}

}
