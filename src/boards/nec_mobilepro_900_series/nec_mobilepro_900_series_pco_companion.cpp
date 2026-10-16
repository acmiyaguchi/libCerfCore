#include "nec_mobilepro_900_series_pco_companion.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>
#include "../../socs/pxa255/pxa255_btuart.h"
#include "../../socs/pxa255/pxa255_gpio.h"
#include "../../socs/rated_tick_count.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"
#include "nec_mobilepro_900_series_led.h"
#include "nec_mobilepro_900_series_pic_loader.h"

#include <algorithm>
#include <utility>

namespace {

using KeyMatrix = NecMobilepro900SeriesPcoCompanion::KeyMatrix;

constexpr uint8_t               kStubReportPeriodMs = 3u;
constexpr GuestCycleClock::Rate kKeyPassRate{125u, 1u};
constexpr uint64_t              kKeyHoldPasses = 4u;
constexpr uint8_t               kPicFirmwareId[3] = {4u, 21u, 7u};

/* nec_mobilepro_900_ce4_2 pco.dll sub_1BC255C (called from sub_1BC25F4 and the resync
   sub_1BC1A14): GPDR2 |= 0x20, GPSR2 = 0x20, KernelIoControl 0x2F48, GPCR2 = 0x20. */
constexpr uint32_t kPicResetGpio = 69u;

enum class PcoAction : uint8_t {
    Store,
    TouchOn,
    TouchOff,
    ReportPeriod,
    KeyNotifyOn,
    KeyNotifyOff,
    KeyScan,
    Download,
    LedOff,
    LedBlink,
    KeyboardMode,
    Battery,
    FirmwareDate,
    FirmwareId,
    FirmwareBuild,
};

struct PcoCommand {
    uint8_t   opcode;
    uint8_t   data_bytes;
    PcoAction action;
};

/* nec_mobilepro_900_ce4_2 pco.dll sub_1BC1CCC / sub_1BC1D0C / sub_1BC1D50: 0x20 / 0x30 / 0x60 |
   LED mask = off / on / blink. gwes.exe sub_B8CD4 table 0x14E34 sends 0x21 for OffOnBlink 0 and
   0x41 0xFF, 0x61 for 2; sub_B8B40 sends 0x2F. */
constexpr PcoCommand kCommands[] = {
    {0x01u, 0u, PcoAction::TouchOn},      {0x02u, 0u, PcoAction::TouchOff},
    {0x03u, 1u, PcoAction::ReportPeriod}, {0x10u, 0u, PcoAction::KeyNotifyOn},
    {0x11u, 0u, PcoAction::KeyNotifyOff}, {0x13u, 0u, PcoAction::KeyScan},
    {0x14u, 1u, PcoAction::KeyboardMode}, {0x17u, 0u, PcoAction::Download},
    {0x1Bu, 0u, PcoAction::Store},        {0x1Cu, 0u, PcoAction::Store},
    {0x1Du, 0u, PcoAction::Store},
    {0x21u, 0u, PcoAction::LedOff},       {0x2Fu, 0u, PcoAction::LedOff},
    {0x30u, 0u, PcoAction::Store},
    {0x41u, 1u, PcoAction::Store},        {0x42u, 1u, PcoAction::Store},
    {0x44u, 1u, PcoAction::Store},        {0x48u, 1u, PcoAction::Store},
    {0x50u, 1u, PcoAction::Store},        {0x51u, 1u, PcoAction::Store},
    {0x60u, 0u, PcoAction::Store},        {0x61u, 0u, PcoAction::LedBlink},
    {0x70u, 0u, PcoAction::Battery},      {0x71u, 0u, PcoAction::Battery},
    {0x80u, 2u, PcoAction::Store},        {0x81u, 1u, PcoAction::Store},
    {0x82u, 1u, PcoAction::Store},        {0x90u, 0u, PcoAction::FirmwareDate},
    {0x92u, 0u, PcoAction::Store},
    {0x93u, 0u, PcoAction::FirmwareBuild}, {0x94u, 0u, PcoAction::FirmwareId},
    {0xFFu, 0u, PcoAction::Store},
};

const PcoCommand* FindCommand(uint8_t opcode) {
    for (const PcoCommand& c : kCommands)
        if (c.opcode == opcode) return &c;
    return nullptr;
}

bool HasNewKeyDown(const KeyMatrix& prev, const KeyMatrix& cur) {
    for (size_t i = 0; i < cur.size(); ++i)
        if (static_cast<uint8_t>(~cur[i] & prev[i]) != 0u) return true;
    return false;
}

}

REGISTER_SERVICE(NecMobilepro900SeriesPcoCompanion);

bool NecMobilepro900SeriesPcoCompanion::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
}

void NecMobilepro900SeriesPcoCompanion::OnReady() {
    clock_     = &emu_.Get<GuestCycleClock>();
    key_event_ = clock_->Add([this] { SettleKeys(clock_->Cycles()); });
    clock_->RegisterRateListener([this] { ArmKeyDown(); });
    host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    host_requests_->RegisterListener([this] { OnHostRequest(); });
    led_    = &emu_.Get<NecMobilepro900SeriesLed>();
    loader_ = &emu_.Get<NecMobilepro900SeriesPicLoader>();
    auto& btuart = emu_.Get<Pxa255Btuart>();
    BindUart(btuart);
    btuart.SetEndpoint(this);
    emu_.Get<Pxa255Gpio>().RegisterOutputObserver(
        [this](const Pxa2xxGpio::OutputPins& pins, bool resync) { OnResetPin(pins, resync); });
    LOG(Periph, "[PCO] bound as the BTUART's serial endpoint\n");
}

void NecMobilepro900SeriesPcoCompanion::OnResetPin(const Pxa2xxGpio::OutputPins& pins,
                                                   bool resync) {
    if (resync || !pins.Driven(kPicResetGpio)) return;
    const bool high = pins.High(kPicResetGpio);
    if (high && !pic_held_) ResetPic();
    pic_held_ = high;
}

void NecMobilepro900SeriesPcoCompanion::ResetPic() {
    LOG(Periph, "[PCO] PIC reset: GPIO %u driven high\n", kPicResetGpio);
    cmd_len_          = 0u;
    data_left_        = 0u;
    report_period_ms_ = 0u;
    pen_down_         = false;
    touch_enabled_    = true;
    key_notify_       = true;
    stored_data_.fill(0u);
    led_->SetBlinking(false);
    loader_->OnPicReset();
    on_report_rate_();
}

bool NecMobilepro900SeriesPcoCompanion::PicRunning() const {
    return !pic_held_ && !loader_->Active();
}

void NecMobilepro900SeriesPcoCompanion::AttachTouch(std::function<void()> on_report_rate,
                                                    std::function<void()> on_restore) {
    on_report_rate_   = std::move(on_report_rate);
    on_touch_restore_ = std::move(on_restore);
}

void NecMobilepro900SeriesPcoCompanion::OnGuestTx(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) OnBtuartTx(data[i]);
}

/* Microchip DS30292D (PIC16F87X) Table 12-6 (page 126): RCSTA after an MCLR Reset is
   0000 000x, SPEN clear. */
void NecMobilepro900SeriesPcoCompanion::OnBtuartTx(uint8_t b) {
    if (pic_held_) {
        LOG(Periph, "[PCO] byte 0x%02X not received: the PIC is held in reset\n", b);
        return;
    }
    if (loader_->Active()) {
        uint8_t reply = 0u;
        if (loader_->TakeByte(b, &reply)) SendReply(reply, nullptr, 0u);
        return;
    }
    if (data_left_ != 0u) {
        cmd_[cmd_len_++] = b;
        if (--data_left_ == 0u) OnCommand();
        return;
    }
    const PcoCommand* c = FindCommand(b);
    if (c == nullptr)
        emu_.Get<Fatal>().Die("[PCO] command 0x%02X: not modelled", b);
    cmd_[0]    = b;
    cmd_len_   = 1u;
    data_left_ = c->data_bytes;
    if (data_left_ == 0u) OnCommand();
}

void NecMobilepro900SeriesPcoCompanion::OnCommand() {
    switch (FindCommand(cmd_[0])->action) {
    case PcoAction::TouchOn:
        touch_enabled_ = true;
        break;
    case PcoAction::TouchOff:
        touch_enabled_ = false;
        break;
    case PcoAction::ReportPeriod:
        if (cmd_[1] == 0u) emu_.Get<Fatal>().Die("[PCO] command 0x03: report period 0 ms");
        report_period_ms_ = cmd_[1];
        on_report_rate_();
        break;
    case PcoAction::KeyNotifyOn:
        key_notify_ = true;
        break;
    case PcoAction::KeyNotifyOff:
        key_notify_ = false;
        break;
    case PcoAction::Download:
        SendReply(loader_->Enter(), nullptr, 0u);
        break;
    case PcoAction::LedOff:
        led_->SetBlinking(false);
        break;
    case PcoAction::LedBlink:
        led_->SetBlinking(true);
        break;
    /* nec_mobilepro_900_ce4_2 pco.dll sub_1BC1A14 resends 0x14 byte_1BC4059 (image value 1);
       sub_1BC1CA0 changes it only on IOCTL 0x16, from backlite.dll sub_1C42834 (keybddr bit 102). */
    case PcoAction::KeyboardMode:
        if (cmd_[1] != 1u)
            emu_.Get<Fatal>().Die("[PCO] command 0x14 0x%02X: keyboard mode not modelled", cmd_[1]);
        break;
    case PcoAction::KeyScan: {
        SettleKeys(clock_->Cycles());
        std::lock_guard<std::mutex> lk(report_mtx_);
        PushByte(0x13u);
        for (uint8_t v : cur_matrix_) PushByte(v);
        break;
    }
    case PcoAction::Battery: {
        const uint16_t raw     = main_battery_raw_.load(std::memory_order_acquire);
        const uint8_t  data[2] = {static_cast<uint8_t>(raw >> 8), static_cast<uint8_t>(raw)};
        SendReply(0x70u, data, 2u);
        break;
    }
    case PcoAction::FirmwareDate: {
        const uint8_t data[2] = {0u, 0u};
        SendReply(0x90u, data, 2u);
        break;
    }
    case PcoAction::FirmwareId:
        SendReply(0x94u, kPicFirmwareId, 2u);
        break;
    case PcoAction::FirmwareBuild:
        SendReply(0x93u, &kPicFirmwareId[2], 1u);
        break;
    case PcoAction::Store:
        stored_data_[2u * cmd_[0]]      = cmd_len_ > 1u ? cmd_[1] : 0u;
        stored_data_[2u * cmd_[0] + 1u] = cmd_len_ > 2u ? cmd_[2] : 0u;
        break;
    }
}

void NecMobilepro900SeriesPcoCompanion::SendReply(uint8_t id, const uint8_t* data, size_t n) {
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(id);
    for (size_t i = 0; i < n; ++i) PushByte(data[i]);
}

void NecMobilepro900SeriesPcoCompanion::PushByte(uint8_t b) {
    uart_->PushRx(&b, 1);
}

uint8_t NecMobilepro900SeriesPcoCompanion::KeyMatrixByte(size_t i) {
    SettleKeys(clock_->Cycles());
    std::lock_guard<std::mutex> lk(report_mtx_);
    return cur_matrix_[i];
}

GuestCycleClock::Rate NecMobilepro900SeriesPcoCompanion::ReportRate() const {
    const uint8_t period = report_period_ms_ != 0u ? report_period_ms_ : kStubReportPeriodMs;
    return GuestCycleClock::Rate{1000u, period};
}

void NecMobilepro900SeriesPcoCompanion::SendTouch(uint16_t x, uint16_t y) {
    if (!PicRunning() || !touch_enabled_) return;
    pen_down_ = true;
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(0x04u);
    PushByte(static_cast<uint8_t>(x >> 8));
    PushByte(static_cast<uint8_t>(x));
    PushByte(static_cast<uint8_t>(y >> 8));
    PushByte(static_cast<uint8_t>(y));
}

void NecMobilepro900SeriesPcoCompanion::SendPenUp() {
    if (!pen_down_ || !PicRunning()) return;
    pen_down_ = false;
    std::lock_guard<std::mutex> lk(report_mtx_);
    PushByte(0x05u);
}

void NecMobilepro900SeriesPcoCompanion::QueueKeyState(const KeyMatrix& matrix) {
    {
        std::lock_guard<std::mutex> lk(key_mtx_);
        if (!host_keys_.empty() && host_keys_.back() == matrix) return;
        host_keys_.push_back(matrix);
    }
    host_requests_->Request();
}

void NecMobilepro900SeriesPcoCompanion::OnHostRequest() {
    std::deque<KeyMatrix> keys;
    {
        std::lock_guard<std::mutex> lk(key_mtx_);
        keys.swap(host_keys_);
    }
    if (keys.empty()) return;
    const uint64_t now = clock_->Cycles();
    for (const KeyMatrix& m : keys) {
        if (m == last_queued_) continue;
        key_steps_.push_back(KeyStep{m, now, HasNewKeyDown(last_queued_, m)});
        last_queued_ = m;
    }
    SettleKeys(now);
}

uint64_t NecMobilepro900SeriesPcoCompanion::HoldEndFrom(uint64_t start) const {
    RatedTickCount hold;
    hold.SetRate(clock_->ClockRate(), kKeyPassRate);
    hold.Start(start);
    return hold.CycleOfTick(kKeyHoldPasses);
}

void NecMobilepro900SeriesPcoCompanion::SettleKeys(uint64_t now) {
    while (!key_steps_.empty()) {
        const KeyStep  step = key_steps_.front();
        const uint64_t at   = key_presented_ ? std::max(step.arrival, HoldEndFrom(cur_at_))
                                             : step.arrival;
        if (at > now) break;
        key_steps_.pop_front();
        Present(step, at);
    }
    ArmKeyDown();
}

void NecMobilepro900SeriesPcoCompanion::Present(const KeyStep& step, uint64_t at) {
    {
        std::lock_guard<std::mutex> lk(report_mtx_);
        cur_matrix_ = step.matrix;
        if (step.key_down && key_notify_ && PicRunning()) PushByte(0x12u);
    }
    cur_at_        = at;
    key_presented_ = true;
}

void NecMobilepro900SeriesPcoCompanion::ArmKeyDown() {
    uint64_t prev_at   = cur_at_;
    bool     presented = key_presented_;
    for (const KeyStep& step : key_steps_) {
        const uint64_t at = presented ? std::max(step.arrival, HoldEndFrom(prev_at)) : step.arrival;
        if (step.key_down) {
            clock_->Arm(key_event_, at);
            return;
        }
        prev_at   = at;
        presented = true;
    }
    clock_->Disarm(key_event_);
}

void NecMobilepro900SeriesPcoCompanion::ResetKeys() {
    clock_->Disarm(key_event_);
    {
        std::lock_guard<std::mutex> lk(key_mtx_);
        host_keys_.clear();
    }
    key_steps_.clear();
    {
        std::lock_guard<std::mutex> lk(report_mtx_);
        cur_matrix_ = kIdleMatrix;
    }
    last_queued_   = kIdleMatrix;
    cur_at_        = 0u;
    key_presented_ = false;
}

void NecMobilepro900SeriesPcoCompanion::SaveState(StateWriter& w) {
    w.Write<uint8_t>("pco_report_period_ms", report_period_ms_);
    w.Write<uint8_t>("pco_cmd0", cmd_[0]);
    w.Write<uint8_t>("pco_cmd1", cmd_[1]);
    w.Write<uint8_t>("pco_cmd2", cmd_[2]);
    w.Write<uint8_t>("pco_cmd_len", cmd_len_);
    w.Write<uint8_t>("pco_data_left", data_left_);
    w.Write<uint8_t>("pco_pen_down", pen_down_ ? 1u : 0u);
    w.Write<uint8_t>("pco_touch_enabled", touch_enabled_ ? 1u : 0u);
    w.Write<uint8_t>("pco_key_notify", key_notify_ ? 1u : 0u);
    w.Write<uint8_t>("pco_pic_held", pic_held_ ? 1u : 0u);
    w.Write<uint8_t>("pco_led_blinking", led_->Blinking() ? 1u : 0u);
    w.WriteBytes("pco_stored_data", stored_data_.data(), stored_data_.size());
    loader_->SaveState(w);
}

void NecMobilepro900SeriesPcoCompanion::RestoreState(StateReader& r) {
    uint8_t down     = 0;
    uint8_t enabled  = 0;
    uint8_t notify   = 0;
    uint8_t held     = 0;
    uint8_t blinking = 0;
    r.Read("pco_report_period_ms", report_period_ms_);
    r.Read("pco_cmd0", cmd_[0]);
    r.Read("pco_cmd1", cmd_[1]);
    r.Read("pco_cmd2", cmd_[2]);
    r.Read("pco_cmd_len", cmd_len_);
    r.Read("pco_data_left", data_left_);
    r.Read("pco_pen_down", down);
    r.Read("pco_touch_enabled", enabled);
    r.Read("pco_key_notify", notify);
    r.Read("pco_pic_held", held);
    r.Read("pco_led_blinking", blinking);
    r.ReadBytes("pco_stored_data", stored_data_.data(), stored_data_.size());
    loader_->RestoreState(r);
    pen_down_      = down != 0u;
    touch_enabled_ = enabled != 0u;
    key_notify_    = notify != 0u;
    pic_held_      = held != 0u;
    led_->SetBlinking(blinking != 0u);
    on_touch_restore_();
    ResetKeys();
}

void NecMobilepro900SeriesPcoCompanion::PostRestore() {
    SendPenUp();
}
