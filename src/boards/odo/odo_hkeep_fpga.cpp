#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "odo_family_id.h"
#include <cerfcore/host/host_widget.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kHkeepFpgaPaBase   = 0x04040000u;
constexpr uint32_t kHkeepFpgaSize     = 0x00030000u;

constexpr uint32_t kLedDiscretePa     = 0x04040000u;
constexpr uint32_t kLedAlphaPa        = 0x04060000u;
constexpr uint32_t kLedAlphaMirror    = 0x0000FFFFu;

class OdoHkeepFpga : public Peripheral, public HostWidget {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Odo;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<HostWidgetRegistry>().Register(this);
    }

    uint32_t MmioBase() const override { return kHkeepFpgaPaBase; }
    uint32_t MmioSize() const override { return kHkeepFpgaSize; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* State image: the two guest-written LED registers. last_drawn_*
       are UI-thread display latches, not guest state. */
    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        w.Write<uint32_t>("led_discrete_value", led_discrete_value_);
        w.Write<uint32_t>("led_alpha_value", led_alpha_value_);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.Read("led_discrete_value", led_discrete_value_);
        r.Read("led_alpha_value", led_alpha_value_);
    }

    /* HostWidget. The icon IS the LED - the discrete LED register lights it,
       the 4-char alpha display shows in the tooltip. No RX/TX. */
    std::string WidgetName() const override { return "Debug LED"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override;
    WidgetIcon  Icon() const override;
    bool PollDirty() override;

private:
    bool ClassifyAddr(uint32_t addr, bool* out_is_alpha) const;

    void LogAlphaWrite(uint32_t addr, uint32_t value);

    mutable std::mutex state_mutex_;
    uint32_t           led_discrete_value_ = 0;
    uint32_t           led_alpha_value_    = 0;

    /* UI-thread only: last values the widget drew, so PollDirty repaints only
       on change. Init to impossible so the first poll always repaints. */
    uint32_t last_drawn_discrete_ = ~0u;
    uint32_t last_drawn_alpha_    = ~0u;
};

bool OdoHkeepFpga::ClassifyAddr(uint32_t addr, bool* out_is_alpha) const {
    if (addr == kLedDiscretePa) {
        *out_is_alpha = false;
        return true;
    }
    /* Alpha mirror: LED_ALPHA + (offset & LED_ALPHA_MIRROR).
       Any DWORD-aligned address in [LED_ALPHA, LED_ALPHA + 0xFFFF]
       hits the single alpha register. */
    if (addr >= kLedAlphaPa && addr <= kLedAlphaPa + kLedAlphaMirror) {
        *out_is_alpha = true;
        return true;
    }
    return false;
}

uint32_t OdoHkeepFpga::ReadWord(uint32_t addr) {
    bool is_alpha = false;
    if (!ClassifyAddr(addr, &is_alpha)) {
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    uint32_t value;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        value = is_alpha ? led_alpha_value_ : led_discrete_value_;
    }

#if CERF_DEV_MODE
    LOG(Board, "Odo HKEEP %s read  [0x%08X] -> 0x%08X\n",
        is_alpha ? "LED_ALPHA" : "LED_DISCRETE", addr, value);
#endif
    return value;
}

void OdoHkeepFpga::WriteWord(uint32_t addr, uint32_t value) {
    bool is_alpha = false;
    if (!ClassifyAddr(addr, &is_alpha)) {
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        if (is_alpha) led_alpha_value_    = value;
        else          led_discrete_value_ = value;
    }

    if (is_alpha) {
        LogAlphaWrite(addr, value);
    } else {
#if CERF_DEV_MODE
        /* Same 1 kHz consideration as the read path. */
        LOG(Board, "Odo HKEEP LED_DISCRETE = 0x%08X\n", value);
#endif
    }
}

void OdoHkeepFpga::LogAlphaWrite(uint32_t addr, uint32_t value) {
    /* LED_ALPHA writes are LOW-frequency on this build path
       (boot stub WhereAmI, OEMInit's 0xECEEECEE, and the very
       rare OEMWriteDebugLED-triggered prints). Permanent LOG is
       acceptable - these are major boot landmarks. */
    char as_ascii[5] = {
        static_cast<char>((value      ) & 0xFFu),
        static_cast<char>((value >>  8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu),
        0,
    };
    auto sanitize = [](char c) -> char {
        return (c >= 0x20 && c < 0x7F) ? c : '.';
    };
    const uint32_t offset = addr - kLedAlphaPa;
    LOG(Board, "Odo HKEEP LED_ALPHA +0x%04X = 0x%08X (\"%c%c%c%c\")\n",
        offset, value,
        sanitize(as_ascii[0]),
        sanitize(as_ascii[1]),
        sanitize(as_ascii[2]),
        sanitize(as_ascii[3]));
}

std::string OdoHkeepFpga::Tooltip() const {
    uint32_t discrete, alpha;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        discrete = led_discrete_value_;
        alpha    = led_alpha_value_;
    }
    auto sane = [](uint32_t v, int shift) -> char {
        const char c = static_cast<char>((v >> shift) & 0xFFu);
        return (c >= 0x20 && c < 0x7F) ? c : '.';
    };
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Debug LED  '%c%c%c%c'  discrete=0x%08X",
                  sane(alpha, 0), sane(alpha, 8), sane(alpha, 16), sane(alpha, 24),
                  discrete);
    return buf;
}

WidgetIcon OdoHkeepFpga::Icon() const {
    uint32_t discrete;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        discrete = led_discrete_value_;
    }
    return LedGridIcon{static_cast<uint8_t>(discrete & 0xFFu), WidgetRgb{255, 180, 40},
                       WidgetRgb{60, 50, 30}, WidgetRgb{90, 75, 40}};
}

bool OdoHkeepFpga::PollDirty() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (led_discrete_value_ == last_drawn_discrete_ &&
        led_alpha_value_    == last_drawn_alpha_) {
        return false;
    }
    last_drawn_discrete_ = led_discrete_value_;
    last_drawn_alpha_    = led_alpha_value_;
    return true;
}

}  /* namespace */

REGISTER_SERVICE(OdoHkeepFpga);

}
