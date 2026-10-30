#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "usb_cdc_acm_host.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

namespace cerfcore {

namespace {

/* PSTN 1.2 section 6.3.11 Table 17: dwDTERate, bCharFormat, bParityType, bDataBits. */
constexpr uint32_t kCodingSize = 7u;

}  /* namespace */

void UsbCdcAcmHost::SetLineCoding(const uint8_t* coding) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::copy(coding, coding + kCodingSize, coding_);
    }
    const LineConfig c = GetLineConfig();
    LineConfigFn cb;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cb = on_line_;
    }
    if (cb) cb(c);
}

SerialLine::LineConfig UsbCdcAcmHost::GetLineConfig() const {
    using Parity = LineConfig::Parity;
    using Stop   = LineConfig::Stop;
    static constexpr Parity kParity[] = {Parity::None, Parity::Odd, Parity::Even, Parity::Mark, Parity::Space};
    static constexpr Stop   kStop[]   = {Stop::One, Stop::OnePointFive, Stop::Two};
    uint8_t k[kCodingSize];
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::copy(coding_, coding_ + kCodingSize, k);
    }
    const uint8_t format = k[4], parity = k[5], bits = k[6];
    if (format > 2u || parity > 4u || bits < 5u || bits > 8u) {
        emu_.Get<Fatal>().Die("UsbCdcAcmHost: line coding format %u parity %u data bits %u", format, parity,
                              bits);
    }
    LineConfig c;
    c.baud      = k[0] | (k[1] << 8) | (k[2] << 16) | (static_cast<uint32_t>(k[3]) << 24);
    c.data_bits = bits;
    c.parity    = kParity[parity];
    c.stop      = kStop[format];
    return c;
}

void UsbCdcAcmHost::Start(uint32_t in_token, uint32_t out_token, uint32_t max_packet) {
    in_token_   = in_token;
    out_token_  = out_token;
    max_packet_ = max_packet;
    in_.assign(max_packet, 0u);
}

void UsbCdcAcmHost::Open(const std::string& host_path) {
    Close();
    if (host_path.empty()) return;
    forward_ = std::make_unique<HostSerialForward>(host_path, emu_, false);
    const uint64_t gen = ++generation_;
    forward_->SetOnBridgeDead([dead = dead_generation_, gen] { dead->store(gen); });
    forward_->SetDeadNotice("The USB serial bridge has been closed. It opens again the next time the device "
                            "enumerates on the virtual USB host.");
    forward_->BindUart(*this);
    forward_->OnOpen();
}

void UsbCdcAcmHost::Close() {
    ++generation_;
    if (forward_) {
        forward_->OnClose();
        forward_.reset();
    }
    std::lock_guard<std::mutex> lk(mu_);
    to_device_.clear();
}

void UsbCdcAcmHost::Stop() {
    Close();
    std::lock_guard<std::mutex> lk(mu_);
    std::fill(std::begin(coding_), std::end(coding_), uint8_t{0});
}

void UsbCdcAcmHost::PushRx(const uint8_t* data, size_t n) {
    std::lock_guard<std::mutex> lk(mu_);
    to_device_.insert(to_device_.end(), data, data + n);
}

bool UsbCdcAcmHost::RxEmpty() const {
    std::lock_guard<std::mutex> lk(mu_);
    return to_device_.empty();
}

void UsbCdcAcmHost::SetRxDrainCallback(RxDrainFn cb) {
    std::lock_guard<std::mutex> lk(mu_);
    on_drain_ = std::move(cb);
}

/* PSTN 1.2 section 6.3.12 Table 18: the host sends the device only DTR (D0) and RTS (D1). */
void UsbCdcAcmHost::SetModemInputs(bool, bool, bool, bool) {}

void UsbCdcAcmHost::SetLineConfigCallback(LineConfigFn cb) {
    std::lock_guard<std::mutex> lk(mu_);
    on_line_ = std::move(cb);
}

/* USB 2.0 section 5.8.3: a bulk transfer ends on a short packet. */
bool UsbCdcAcmHost::Step(const Transact& port) {
    if (forward_ && dead_generation_->load() == generation_.load()) Close();
    bool busy = false;
    std::vector<uint8_t> out;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const size_t n = std::min<size_t>(to_device_.size(), max_packet_ - 1u);
        out.assign(to_device_.begin(), to_device_.begin() + static_cast<std::ptrdiff_t>(n));
    }
    if (!out.empty()) {
        Result r{};
        if (port(out_token_, out.data(), static_cast<uint32_t>(out.size()), r)) {
            RxDrainFn drained;
            {
                std::lock_guard<std::mutex> lk(mu_);
                to_device_.erase(to_device_.begin(), to_device_.begin() + r.moved);
                if (to_device_.empty()) drained = on_drain_;
            }
            if (drained) drained();
            busy = true;
        }
    }
    Result r{};
    if (port(in_token_, in_.data(), max_packet_, r) && r.moved != 0u) {
        if (forward_) forward_->OnGuestTx(in_.data(), r.moved);
        else          LOG(UsbOtg, "UsbCdcAcmHost: %u IN bytes with no host path\n", r.moved);
        busy = true;
    }
    return busy;
}

void UsbCdcAcmHost::SaveState(StateWriter& w) {
    w.Write("cdc_in", in_token_);
    w.Write("cdc_out", out_token_);
    w.Write("cdc_max_packet", max_packet_);
    std::lock_guard<std::mutex> lk(mu_);
    w.WriteBytes("cdc_coding", coding_, kCodingSize);
}

void UsbCdcAcmHost::RestoreState(StateReader& r) {
    Stop();
    r.Read("cdc_in", in_token_);
    r.Read("cdc_out", out_token_);
    r.Read("cdc_max_packet", max_packet_);
    in_.assign(max_packet_, 0u);
    uint8_t k[kCodingSize];
    r.ReadBytes("cdc_coding", k, kCodingSize);
    std::lock_guard<std::mutex> lk(mu_);
    std::copy(k, k + kCodingSize, coding_);
}

}
