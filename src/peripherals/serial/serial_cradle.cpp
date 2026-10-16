#include "serial_cradle.h"

#include "host_serial_forward.h"
#include "modem_personality.h"
#include "serial_endpoint.h"
#include "serial_endpoint_kind.h"
#include "serial_forward_insert_entry.h"
#include "serial_line.h"
#include "serial_modem_insert_entry.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <utility>

namespace cerfcore {

SerialCradle::SerialCradle(CerfEmulator& emu, SerialLine& line, std::string label)
    : emu_(emu), line_(line), label_(std::move(label)) {}

SerialCradle::~SerialCradle() = default;

void SerialCradle::OnShutdown() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (endpoint_) {
        line_.SetEndpoint(nullptr);
        endpoint_->OnClose();
        endpoint_.reset();
    }
    kind_ = Kind::None;
}

void SerialCradle::SaveCradleState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write<uint8_t>("kind", static_cast<uint8_t>(kind_));
    const std::u16string port = Utf8ToUtf16(host_port_);
    const uint32_t n = static_cast<uint32_t>(port.size());
    w.Write("host_port_count", n);
    if (n) w.WriteBytes("host_port", port.data(), n * sizeof(char16_t));
}

void SerialCradle::RestoreCradleState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    uint8_t k = 0;
    r.Read("kind", k);
    restored_kind_ = static_cast<Kind>(k);

    uint32_t n = 0;
    r.Read("host_port_count", n);
    std::u16string port(n, u'\0');
    if (n) r.ReadBytes("host_port", port.data(), n * sizeof(char16_t));
    restored_port_ = Utf16ToUtf8(port);
}

void SerialCradle::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    SetPluggedLocked(restored_kind_, restored_port_);
}

void SerialCradle::SetPluggedLocked(Kind kind, std::string host_port) {
    if (endpoint_) {
        line_.SetEndpoint(nullptr);
        endpoint_->OnClose();
        endpoint_.reset();
    }

    kind_      = kind;
    host_port_ = std::move(host_port);
    ++generation_;

    if (kind == Kind::None) return;

    if (kind == Kind::Modem) {
        endpoint_ = std::make_unique<ModemPersonality>(emu_, "ppp:" + label_);
    } else {
        auto fwd = std::make_unique<HostSerialForward>(host_port_, emu_);
        const uint64_t gen = generation_;
        fwd->SetOnBridgeDead([this, gen] { Eject(gen); });
        endpoint_ = std::move(fwd);
    }

    endpoint_->BindUart(line_);
    endpoint_->OnOpen();
    line_.SetEndpoint(endpoint_.get());
}


void SerialCradle::SwapModem(uint64_t gen) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (generation_ != gen) return;
    SetPluggedLocked(Kind::Modem, {});
}

void SerialCradle::SwapForward(uint64_t gen, std::string host_port) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (generation_ != gen) return;
    SetPluggedLocked(Kind::Forward, std::move(host_port));
}

void SerialCradle::Eject(uint64_t gen) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (generation_ != gen) return;
    SetPluggedLocked(Kind::None, {});
}

std::vector<SlotEntry> SerialCradle::AvailableLocked(uint64_t gen) {
    SlotEntry modem = emu_.Get<SerialModemInsertEntry>().BuildInsertEntry(
        [this, gen] { SwapModem(gen); });
    modem.id    = kModemId;
    modem.label = serial_endpoint_kind::kModemName;

    SlotEntry fwd = emu_.Get<SerialForwardInsertEntry>().BuildInsertEntry(
        [this, gen](std::string port) { SwapForward(gen, std::move(port)); });
    fwd.id    = kForwardId;
    fwd.label = serial_endpoint_kind::kForwardName;

    std::vector<SlotEntry> entries;
    entries.push_back(std::move(modem));
    entries.push_back(std::move(fwd));
    return entries;
}

std::vector<WidgetControl> SerialCradle::Controls() {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint64_t gen = generation_;

    SlotControl slot;
    slot.id        = "slot";
    slot.kind      = SlotKind::kSerialPort;
    slot.available = AvailableLocked(gen);
    if (kind_ == Kind::Modem)
        slot.inserted = ChoiceOption{kModemId, serial_endpoint_kind::kModemName};
    else if (kind_ == Kind::Forward)
        slot.inserted = ChoiceOption{kForwardId, serial_endpoint_kind::kForwardName};
    if (kind_ != Kind::None) slot.eject = [this, gen] { Eject(gen); };
    return {WidgetControl{std::move(slot)}};
}

const char* SerialCradle::IconResourceLocked() const {
    switch (kind_) {
        case Kind::Modem:   return "ICON_SERIAL_MODEM";
        case Kind::Forward: return "ICON_SERIAL_COM";
        case Kind::None:    return "ICON_SERIAL_EMPTY";
    }
    return "ICON_SERIAL_EMPTY";
}

WidgetIcon SerialCradle::Icon() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ResourceIcon{IconResourceLocked()};
}

bool SerialCradle::PollDirty() {
    std::string res;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        res = IconResourceLocked();
    }
    if (res == ui_last_res_) return false;
    ui_last_res_ = std::move(res);
    return true;
}

std::string SerialCradle::Tooltip() const {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (kind_) {
        case Kind::Modem:
            return label_ + " - " + serial_endpoint_kind::kModemName;
        case Kind::Forward:
            return label_ + " - " + serial_endpoint_kind::kForwardName +
                   " -> host " + host_port_;
        case Kind::None:
            return label_ + " - empty";
    }
    return label_;
}

}
