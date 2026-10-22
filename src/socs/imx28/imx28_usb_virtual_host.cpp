#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_id.h"
#include "../../peripherals/usb/usb_hex.h"
#include "imx28_usb_virtual_host.h"
#include "imx28_usbctrl.h"

#include <algorithm>
#include <cstdint>

namespace cerfcore {

namespace {

/* USB 2.0 section 7.1.7.3 TATTDB 100 ms; section 7.1.7.5 TDRSTR 50 ms and TRSTRCY 10 ms;
   section 9.2.6.3 SetAddress recovery 2 ms; section 8.4.3.1 microframe 125 us. */
constexpr int64_t kAttachNs   = 100000000;
constexpr int64_t kResetNs    = 50000000;
constexpr int64_t kRecoveryNs = 10000000;
constexpr int64_t kAddressNs  = 2000000;
constexpr int64_t kFrameNs    = 125000;
constexpr uint32_t kAddressPolls = 8000u;
constexpr uint32_t kMaxResets    = 3u;

/* Section 31.3.39 ENDPTCOMPLETE: EP0 RX bit 0, EP0 TX bit 16. */
constexpr uint32_t kEp0Rx = 0u;
constexpr uint32_t kEp0Tx = 16u;

/* USB 2.0 Table 9-2 bmRequestType [7] device-to-host, Table 9-4 SET_ADDRESS 5, GET_DESCRIPTOR 6,
   SET_CONFIGURATION 9; Table 9-5 DEVICE 1, CONFIGURATION 2, INTERFACE 4, ENDPOINT 5. */
constexpr uint8_t  kDirIn         = 0x80u;
constexpr uint8_t  kSetAddress    = 5u;
constexpr uint8_t  kGetDescriptor = 6u;
constexpr uint8_t  kSetConfig     = 9u;
constexpr uint8_t  kDescDevice    = 1u;
constexpr uint8_t  kDescConfig    = 2u;
constexpr uint8_t  kDescInterface = 4u;
constexpr uint8_t  kDescEndpoint  = 5u;
constexpr uint32_t kDeviceLength  = 18u;
constexpr uint32_t kConfigLength  = 9u;
constexpr uint32_t kAddress       = 1u;

/* USB 2.0 Table 9-13 bEndpointAddress [7] IN, [3:0] number; bmAttributes [1:0] 2 Bulk.
   USB MSC Class Specification Overview 1.4 class 08h, SCSI transparent 06h, Bulk-Only 50h. */
constexpr uint8_t  kEpIn          = 0x80u;
constexpr uint8_t  kEpNumberMask  = 0x0Fu;
constexpr uint8_t  kEpTypeMask    = 0x03u;
constexpr uint8_t  kEpBulk        = 2u;
constexpr uint32_t kMscBot        = 0x00080650u;
constexpr uint32_t kNoEndpoint    = 0xFFFFFFFFu;

enum Request : uint32_t { kReqAddress, kReqDevice, kReqConfigHead, kReqConfig, kReqSetConfig };

}  /* namespace */

bool Imx28UsbVirtualHost::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28UsbVirtualHost::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    event_ = clock_->Add([this] { Step(); });
    clock_->RegisterRateListener([this] { Rearm(); });
}

void Imx28UsbVirtualHost::Attach(Imx28Usbctrl& port) {
    port_ = &port;
    port.AttachHost([this] { OnLink(); }, [this] { OnPrime(); });
}

void Imx28UsbVirtualHost::Die(const char* what, uint32_t a, uint32_t b) const {
    emu_.Get<Fatal>().Die("Imx28UsbVirtualHost: %s (%08X %08X, request %u)", what, a, b, request_);
}

void Imx28UsbVirtualHost::Arm(int64_t ns) {
    due_ns_ = clock_->NowNs() + ns;
    armed_  = 1u;
    clock_->Arm(event_, clock_->Cycles() + clock_->NsToCycles(ns));
}

void Imx28UsbVirtualHost::Rearm() {
    if (armed_ != 0u) Arm(std::max<int64_t>(0, due_ns_ - clock_->NowNs()));
}

void Imx28UsbVirtualHost::OnLink() {
    const uint32_t now = port_->Linked() ? 1u : 0u;
    if (now == linked_) return;
    linked_  = now;
    request_ = kReqAddress;
    polls_   = 0u;
    resets_  = 0u;
    if (now != 0u) {
        stage_ = kDebounce;
        Arm(kAttachNs);
    } else {
        stage_ = kIdle;
        armed_ = 0u;
        clock_->Disarm(event_);
    }
}

/* MCIMX28RM Rev. 2 Table 31-67: a primed endpoint answers the next IN or OUT token. */
void Imx28UsbVirtualHost::OnPrime() {
    if (stage_ == kData || stage_ == kStatus || stage_ == kBulk) Arm(kFrameNs);
}

void Imx28UsbVirtualHost::Step() {
    armed_ = 0u;
    switch (stage_) {
        case kDebounce:
            port_->BusReset();
            stage_ = kReset;
            Arm(kResetNs);
            return;
        case kReset:
            port_->BusResetEnd();
            stage_ = kRecovery;
            Arm(kRecoveryNs);
            return;
        case kRecovery: StartRequest(); return;
        case kData:     StepData(); return;
        case kStatus:   StepStatus(); return;
        case kBulk:
            if (bot_.Step([this](uint32_t bit, uint8_t* data, uint32_t length, UsbBotHost::Result& out) {
                    Imx28UsbEndpoints::Transfer t{};
                    const bool done = port_->Transact(bit, data, length, t);
                    out = {t.moved, t.short_packet};
                    return done;
                })) {
                Arm(kFrameNs);
            }
            return;
        default:        return;
    }
}

/* USB 2.0 section 9.3 Table 9-2 setup data: bmRequestType, bRequest, wValue, wIndex, wLength. */
void Imx28UsbVirtualHost::StartRequest() {
    const uint32_t addr = request_ == kReqAddress ? 0u : kAddress;
    if (port_->DeviceAddress() != addr) {
        if (++polls_ < kAddressPolls) {
            Arm(kFrameNs);
            return;
        }
        RetryEnumeration();
        return;
    }
    polls_ = 0u;
    uint8_t  type = kDirIn, req = kGetDescriptor;
    uint32_t value = 0u;
    switch (request_) {
        case kReqAddress:    type = 0u; req = kSetAddress; value = kAddress; length_ = 0u; break;
        case kReqDevice:     value = kDescDevice << 8; length_ = kDeviceLength; break;
        case kReqConfigHead: value = kDescConfig << 8; length_ = kConfigLength; break;
        case kReqConfig:
            value   = kDescConfig << 8;
            length_ = config_[2] | (config_[3] << 8);
            break;
        case kReqSetConfig:  type = 0u; req = kSetConfig; value = config_[5]; length_ = 0u; break;
        default:
            StartBulk();
            return;
    }
    const uint8_t setup[8] = {type, req, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                              0u, 0u, static_cast<uint8_t>(length_), static_cast<uint8_t>(length_ >> 8)};
    std::copy(setup, setup + 8, setup_);
    LOG(UsbOtg, "Imx28UsbVirtualHost: SETUP %02X %02X %04X wLength %u\n", type, req, value, length_);
    data_.clear();
    port_->Setup(setup);
    stage_ = (type & kDirIn) != 0u && length_ != 0u ? kData : kStatus;
    Arm(kFrameNs);
}

/* USB 2.0 section 9.6.5 interface descriptor [5..7] class/subclass/protocol; section 9.6.6 endpoint
   descriptor [2] bEndpointAddress, [3] bmAttributes. MCIMX28RM Rev. 2 section 31.3.36: RX bit n, TX bit 16+n. */
void Imx28UsbVirtualHost::StartBulk() {
    uint32_t iface = 0u, in_bit = kNoEndpoint, out_bit = kNoEndpoint;
    for (size_t i = 0; i + 3 < config_.size() && config_[i] != 0u; i += config_[i]) {
        if (config_[i + 1] == kDescInterface && i + 7 < config_.size()) {
            iface = (config_[i + 5] << 16) | (config_[i + 6] << 8) | config_[i + 7];
        }
        if (config_[i + 1] == kDescEndpoint && (config_[i + 3] & kEpTypeMask) == kEpBulk) {
            const uint32_t n = config_[i + 2] & kEpNumberMask;
            if ((config_[i + 2] & kEpIn) != 0u) in_bit = 16u + n;
            else                                out_bit = n;
        }
    }
    if (iface != kMscBot || in_bit == kNoEndpoint || out_bit == kNoEndpoint) {
        Die("interface class/subclass/protocol, bulk IN/OUT", iface, (in_bit << 16) | (out_bit & 0xFFFFu));
    }
    LOG(UsbOtg, "Imx28UsbVirtualHost: bulk-only mass storage, IN bit %u OUT bit %u\n", in_bit, out_bit);
    bot_.Start(in_bit, out_bit);
    stage_ = kBulk;
    Arm(kFrameNs);
}

void Imx28UsbVirtualHost::RetryEnumeration() {
    polls_   = 0u;
    request_ = kReqAddress;
    LOG(UsbOtg, "Imx28UsbVirtualHost: device address %u not set, reset %u\n", port_->DeviceAddress(),
        resets_);
    if (++resets_ > kMaxResets) {
        stage_ = kIdle;
        return;
    }
    port_->BusReset();
    stage_ = kReset;
    Arm(kResetNs);
}

/* USB 2.0 section 5.5.3: the data stage ends on wLength bytes or a short packet. */
void Imx28UsbVirtualHost::StepData() {
    std::vector<uint8_t> buf(length_ - data_.size());
    Imx28UsbEndpoints::Transfer t{};
    if (!port_->Transact(kEp0Tx, buf.data(), static_cast<uint32_t>(buf.size()), t)) return;
    data_.insert(data_.end(), buf.begin(), buf.begin() + t.moved);
    if (t.short_packet || data_.size() == length_) stage_ = kStatus;
    Arm(kFrameNs);
}

/* USB 2.0 section 8.5.3: the status stage runs opposite to the data stage, IN without data. */
void Imx28UsbVirtualHost::StepStatus() {
    Imx28UsbEndpoints::Transfer t{};
    uint8_t none = 0u;
    const bool in_request = (setup_[0] & kDirIn) != 0u;
    if (!port_->Transact(in_request ? kEp0Rx : kEp0Tx, &none, 0u, t)) return;
    if (t.moved != 0u) Die("status stage bytes", t.moved, setup_[1]);
    FinishRequest();
}

void Imx28UsbVirtualHost::FinishRequest() {
    stage_ = kRecovery;
    switch (request_) {
        case kReqAddress:
            Arm(kAddressNs);
            break;
        case kReqDevice:
            LOG(UsbOtg, "Imx28UsbVirtualHost: device descriptor %s\n", UsbHex(data_.data(), data_.size()).c_str());
            if (data_.size() != kDeviceLength || data_[1] != kDescDevice) {
                Die("device descriptor length/type", static_cast<uint32_t>(data_.size()), data_[1]);
            }
            Arm(kFrameNs);
            break;
        case kReqConfigHead:
        case kReqConfig:
            LOG(UsbOtg, "Imx28UsbVirtualHost: configuration descriptor %s\n", UsbHex(data_.data(), data_.size()).c_str());
            if (data_.size() < kConfigLength || data_[1] != kDescConfig) {
                Die("configuration descriptor length/type", static_cast<uint32_t>(data_.size()), data_[1]);
            }
            config_ = data_;
            Arm(kFrameNs);
            break;
        default:
            Arm(kFrameNs);
            break;
    }
    ++request_;
}

void Imx28UsbVirtualHost::SaveState(StateWriter& w) {
    w.Write("stage", stage_);
    w.Write("request", request_);
    w.Write("length", length_);
    w.Write("armed", armed_);
    w.Write("linked", linked_);
    w.Write("due_ns", due_ns_);
    w.Write("polls", polls_);
    w.Write("resets", resets_);
    w.Write("setup", setup_);
    w.Write("data_size", static_cast<uint32_t>(data_.size()));
    w.WriteBytes("data", data_.data(), data_.size());
    w.Write("config_size", static_cast<uint32_t>(config_.size()));
    w.WriteBytes("config", config_.data(), config_.size());
    bot_.SaveState(w);
}

void Imx28UsbVirtualHost::RestoreState(StateReader& r) {
    uint32_t size = 0u;
    r.Read("stage", stage_);
    r.Read("request", request_);
    r.Read("length", length_);
    r.Read("armed", armed_);
    r.Read("linked", linked_);
    r.Read("due_ns", due_ns_);
    r.Read("polls", polls_);
    r.Read("resets", resets_);
    r.Read("setup", setup_);
    r.Read("data_size", size);
    data_.resize(size);
    r.ReadBytes("data", data_.data(), data_.size());
    r.Read("config_size", size);
    config_.resize(size);
    r.ReadBytes("config", config_.data(), config_.size());
    bot_.RestoreState(r);
}

void Imx28UsbVirtualHost::PostRestore() {
    if (armed_ != 0u) Rearm();
    else              clock_->Disarm(event_);
}

REGISTER_SERVICE(Imx28UsbVirtualHost);

}
