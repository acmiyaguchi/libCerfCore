#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_widget.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace {

constexpr uint32_t kBase = 0x500FFFD0u;
constexpr uint32_t kSize = 0x00000006u;
constexpr uint32_t kNumLeds      = 2u;
constexpr uint32_t kBytesPerLed  = 3u;

constexpr uint32_t kFieldOnOffBlink = 0u;
constexpr uint32_t kFieldOnTime     = 1u;
constexpr uint32_t kFieldOffTime    = 2u;

constexpr uint8_t kOff   = 0u;
constexpr uint8_t kOn     = 1u;
constexpr uint8_t kBlink  = 2u;

/* Fallback blink cadence (ticks on / off) when the driver set blink but left
   the timing fields at zero. */
constexpr uint32_t kDefaultBlinkTicks = 5u;  /* 500 ms */

constexpr WidgetRgb kClrLit{78, 201, 90};
constexpr WidgetRgb kClrDark{50, 55, 50};
constexpr WidgetRgb kClrRimBlink{120, 230, 130};
constexpr WidgetRgb kClrRim{90, 95, 90};

class SamsungSmdk2410NotificationLed : public Peripheral, public HostWidget {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<HostWidgetRegistry>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte (uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        w.WriteBytes("regs", regs_.data(), regs_.size());
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.ReadBytes("regs", regs_.data(), regs_.size());
    }

    /* HostWidget. The icon IS the LED state; no data path -> no RX/TX. */
    std::string WidgetName() const override { return "Notification LED"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override;
    WidgetIcon  Icon() const override;
    bool PollDirty() override;

private:
    static const char* FieldName(uint32_t led_off);

    mutable std::mutex                            state_mutex_;
    std::array<uint8_t, kNumLeds * kBytesPerLed>  regs_{};

    /* UI-thread only: free-running tick + last-drawn appearance. */
    uint32_t tick_           = 0;
    bool     draw_led0_lit_  = false;
    bool     draw_led0_blink_ = false;
    bool     draw_vibrate_   = false;
};

const char* SamsungSmdk2410NotificationLed::FieldName(uint32_t led_off) {
    switch (led_off) {
        case kFieldOnOffBlink: return "OnOffBlink";
        case kFieldOnTime:     return "OnTime";
        case kFieldOffTime:    return "OffTime";
        default:               return "?";
    }
}

uint8_t SamsungSmdk2410NotificationLed::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint8_t value = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (off >= regs_.size()) {
            HaltUnsupportedAccess("ReadByte", addr, 0);  /* noreturn */
        }
        value = regs_[off];
    }
    const uint32_t led     = off / kBytesPerLed;
    const uint32_t led_off = off % kBytesPerLed;
    LOG(Periph, "[NLED] read8 led=%u %s -> 0x%02X\n",
        led, FieldName(led_off), value);
    return value;
}

void SamsungSmdk2410NotificationLed::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off     = addr - kBase;
    const uint32_t led     = off / kBytesPerLed;
    const uint32_t led_off = off % kBytesPerLed;
    LOG(Periph, "[NLED] write8 led=%u %s = 0x%02X\n",
        led, FieldName(led_off), value);
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (off >= regs_.size()) {
        HaltUnsupportedAccess("WriteByte", addr, value);  /* noreturn */
    }
    regs_[off] = value;
}

std::string SamsungSmdk2410NotificationLed::Tooltip() const {
    uint8_t mode0, on0, off0, mode1;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        mode0 = regs_[kFieldOnOffBlink];
        on0   = regs_[kFieldOnTime];
        off0  = regs_[kFieldOffTime];
        mode1 = regs_[kBytesPerLed + kFieldOnOffBlink];
    }
    char buf[128];
    if (mode0 == kBlink) {
        std::snprintf(buf, sizeof(buf), "Notification LED: blink (on %ums / off %ums) · Vibrate: %s",
                      (unsigned)on0 * 100u, (unsigned)off0 * 100u,
                      mode1 != kOff ? "on" : "off");
    } else {
        std::snprintf(buf, sizeof(buf), "Notification LED: %s · Vibrate: %s",
                      mode0 == kOn ? "on" : "off",
                      mode1 != kOff ? "on" : "off");
    }
    return buf;
}

WidgetIcon SamsungSmdk2410NotificationLed::Icon() const {
    constexpr int r = 5;
    return LedIcon{draw_led0_lit_ ? kClrLit : kClrDark,
                   draw_led0_blink_ ? kClrRimBlink : kClrRim, r, draw_vibrate_};
}

bool SamsungSmdk2410NotificationLed::PollDirty() {
    uint8_t mode0, on0, off0, mode1;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        mode0 = regs_[kFieldOnOffBlink];
        on0   = regs_[kFieldOnTime];
        off0  = regs_[kFieldOffTime];
        mode1 = regs_[kBytesPerLed + kFieldOnOffBlink];
    }
    ++tick_;

    bool led0_lit;
    if (mode0 == kBlink) {
        uint32_t on_ticks  = on0;
        uint32_t off_ticks = off0;
        if (on_ticks == 0 && off_ticks == 0) {
            on_ticks = off_ticks = kDefaultBlinkTicks;
        }
        const uint32_t cycle = on_ticks + off_ticks;
        led0_lit = cycle == 0 ? true : (tick_ % cycle) < on_ticks;
    } else {
        led0_lit = mode0 == kOn;
    }
    const bool blink0   = mode0 == kBlink;
    const bool vibrate  = mode1 != kOff;

    if (led0_lit == draw_led0_lit_ && blink0 == draw_led0_blink_ &&
        vibrate == draw_vibrate_) {
        return false;
    }
    draw_led0_lit_   = led0_lit;
    draw_led0_blink_ = blink0;
    draw_vibrate_    = vibrate;
    return true;
}

}  /* namespace */

REGISTER_SERVICE(SamsungSmdk2410NotificationLed);
