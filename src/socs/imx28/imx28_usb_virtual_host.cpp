#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_id.h"
#include "../../peripherals/usb/usb_hex.h"
#include "imx28_usb_virtual_host.h"
#include "imx28_usbctrl.h"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace cerfcore {

namespace {

/* USB 2.0 section 7.1.7.3 TATTDB 100 ms; section 7.1.7.5 TDRSTR 50 ms and TRSTRCY 10 ms;
   section 9.2.6.3 SetAddress recovery 2 ms; section 8.4.3.1 microframe 125 us, section 8.4.3 frame
   1 ms. */
constexpr int64_t kAttachNs   = 100000000;
constexpr int64_t kResetNs    = 50000000;
constexpr int64_t kRecoveryNs = 10000000;
constexpr int64_t kAddressNs  = 2000000;
constexpr int64_t kFrameNs    = 125000;
constexpr int64_t kIdleNs     = 1000000;
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

/* USB CDC 1.2 section 4.5 Table 6: Data Interface Class code 0Ah. */
constexpr uint32_t kCdcDataClass  = 0x0Au;
/* USB CDC 1.2 section 4.2 Table 3, section 4.3 Table 4; PSTN 1.2 section 6.3.11 GET_LINE_CODING. */
constexpr uint32_t kCdcAcm        = 0x0202u;
constexpr uint8_t  kCsInterface   = 0x24u;
constexpr uint8_t  kAcmFunctional = 0x02u;
constexpr uint8_t  kAcmLineCoding = 0x02u;
constexpr uint8_t  kClassIfaceIn  = 0xA1u;
constexpr uint8_t  kGetLineCoding = 0x21u;
constexpr uint32_t kLineCodingLength = 7u;
constexpr uint32_t kNoEndpoint    = 0xFFFFFFFFu;

enum Request : uint32_t { kReqAddress, kReqDevice, kReqConfigHead, kReqConfig, kReqSetConfig, kReqLineCoding };

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

void Imx28UsbVirtualHost::OnShutdown() { cdc_.Stop(); }

/* USB 2.0 section 9.6.5 interface descriptor [2] bInterfaceNumber, [5..6] class/subclass; CDC120
   Tables 12-13 CS_INTERFACE 24h, subtype 02h; PSTN120 Table 4 bmCapabilities D1 Get_Line_Coding. */
uint32_t Imx28UsbVirtualHost::AcmInterface() const {
    uint32_t iface = kNoEndpoint;
    for (size_t i = 0; i + 3 < config_.size() && config_[i] != 0u; i += config_[i]) {
        if (config_[i + 1] == kDescInterface && i + 7 < config_.size()) {
            iface = ((config_[i + 5] << 8) | config_[i + 6]) == kCdcAcm ? config_[i + 2] : kNoEndpoint;
        } else if (iface != kNoEndpoint && config_[i] >= 4u && config_[i + 1] == kCsInterface &&
                   config_[i + 2] == kAcmFunctional && (config_[i + 3] & kAcmLineCoding) != 0u) {
            return iface;
        }
    }
    return kNoEndpoint;
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
        cdc_.Stop();
        cdc_active_ = 0u;
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
            if (cdc_active_ != 0u) {
                const bool busy = cdc_.Step([this](uint32_t bit, uint8_t* data, uint32_t length,
                                                   UsbCdcAcmHost::Result& out) {
                    Imx28UsbEndpoints::Transfer t{};
                    const bool done = port_->Transact(bit, data, length, t);
                    out = {t.moved, t.short_packet};
                    return done;
                });
                Arm(busy ? kFrameNs : kIdleNs);
                return;
            }
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
    uint32_t value = 0u, index = 0u;
    switch (request_) {
        case kReqAddress:    type = 0u; req = kSetAddress; value = kAddress; length_ = 0u; break;
        case kReqDevice:     value = kDescDevice << 8; length_ = kDeviceLength; break;
        case kReqConfigHead: value = kDescConfig << 8; length_ = kConfigLength; break;
        case kReqConfig:
            value   = kDescConfig << 8;
            length_ = config_[2] | (config_[3] << 8);
            break;
        case kReqSetConfig:  type = 0u; req = kSetConfig; value = config_[5]; length_ = 0u; break;
        case kReqLineCoding:
            index = AcmInterface();
            if (index == kNoEndpoint) {
                StartBulk();
                return;
            }
            type = kClassIfaceIn; req = kGetLineCoding; length_ = kLineCodingLength;
            break;
        default:
            StartBulk();
            return;
    }
    const uint8_t setup[8] = {type,
                              req,
                              static_cast<uint8_t>(value),
                              static_cast<uint8_t>(value >> 8),
                              static_cast<uint8_t>(index),
                              static_cast<uint8_t>(index >> 8),
                              static_cast<uint8_t>(length_),
                              static_cast<uint8_t>(length_ >> 8)};
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
    uint32_t iface = 0u, owner = 0u, in_bit = kNoEndpoint, out_bit = kNoEndpoint, max_packet = 0u;
    uint32_t index = kNoEndpoint, owner_index = kNoEndpoint;
    for (size_t i = 0; i + 3 < config_.size() && config_[i] != 0u; i += config_[i]) {
        if (config_[i + 1] == kDescInterface && i + 7 < config_.size()) {
            iface = (config_[i + 5] << 16) | (config_[i + 6] << 8) | config_[i + 7];
            ++index;
        }
        if (config_[i + 1] == kDescEndpoint && i + 5 < config_.size() &&
            (config_[i + 3] & kEpTypeMask) == kEpBulk) {
            const uint32_t n      = config_[i + 2] & kEpNumberMask;
            const bool     in     = (config_[i + 2] & kEpIn) != 0u;
            const uint32_t packet = config_[i + 4] | (config_[i + 5] << 8);
            if ((owner_index != kNoEndpoint && (owner_index != index || packet != max_packet)) ||
                (in ? in_bit : out_bit) != kNoEndpoint) {
                Die("bulk endpoints across interfaces or repeated", index, config_[i + 2]);
            }
            if (in) in_bit = 16u + n;
            else    out_bit = n;
            owner       = iface;
            owner_index = index;
            max_packet  = packet;
        }
    }
    if (in_bit == kNoEndpoint || out_bit == kNoEndpoint) {
        Die("bulk IN/OUT endpoints", in_bit, out_bit);
    }
    /* USB 2.0 section 5.8.3: bulk maximum payload 8, 16, 32 or 64 at full speed, 512 at high speed. */
    const bool full_ok = max_packet >= 8u && max_packet <= 64u && std::has_single_bit(max_packet);
    if (port_->HighSpeed() ? max_packet != 512u : !full_ok) {
        Die("bulk wMaxPacketSize", max_packet, owner);
    }
    if (owner == kMscBot) {
        LOG(UsbOtg, "Imx28UsbVirtualHost: bulk-only mass storage, IN bit %u OUT bit %u\n", in_bit, out_bit);
        cdc_active_ = 0u;
        bot_.Start(in_bit, out_bit);
    } else if ((owner >> 16) == kCdcDataClass && AcmInterface() != kNoEndpoint) {
        LOG(UsbOtg, "Imx28UsbVirtualHost: CDC data interface, IN bit %u OUT bit %u max packet %u\n",
            in_bit, out_bit, max_packet);
        cdc_active_ = 1u;
        cdc_.Start(in_bit, out_bit, max_packet);
        cdc_.Open(emu_.Get<DeviceConfig>().usb_serial_host);
    } else {
        Die("bulk interface class/subclass/protocol", owner, (in_bit << 16) | (out_bit & 0xFFFFu));
    }
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
        case kReqLineCoding:
            if (data_.size() != kLineCodingLength) {
                Die("line coding length", static_cast<uint32_t>(data_.size()), kLineCodingLength);
            }
            cdc_.SetLineCoding(data_.data());
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
    w.Write("cdc_active", cdc_active_);
    cdc_.SaveState(w);
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
    r.Read("cdc_active", cdc_active_);
    cdc_.RestoreState(r);
}

void Imx28UsbVirtualHost::PostRestore() {
    if (cdc_active_ != 0u) cdc_.Open(emu_.Get<DeviceConfig>().usb_serial_host);
    if (armed_ != 0u) Rearm();
    else              clock_->Disarm(event_);
}

REGISTER_SERVICE(Imx28UsbVirtualHost);

}
