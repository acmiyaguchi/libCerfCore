#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../state/state_boot_gate.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../socs/mpu_lcd_panel.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 ddraw_mx28.dll LcdifUpdateRegion c06d7df0 (2Ah end 1DFh),
   UpdateExtraScreen c06d848c (2Bh 320h to 355h). */
constexpr uint32_t kColumns = 480u;
constexpr uint32_t kPages   = 854u;

/* ILI9341 V1.11 sections 8.2.20 CASET, 8.2.21 PASET, 8.2.22 RAMWR (MIPI DCS command set). */
constexpr uint8_t kCaset = 0x2Au;
constexpr uint8_t kPaset = 0x2Bu;
constexpr uint8_t kRamwr = 0x2Cu;

struct SettingCommand {
    uint8_t  cmd;
    uint32_t params;
};

/* sharp_brain_pw_sh4 ddraw_mx28.dll PanelGammaVariantSeq c06d75d0 (BCh 26 parameters);
   ILI9806H V091 pages 268-309 (B1h, B4h, C0h, C1h, C6h, C7h, E0h, E1h).
   C1h 8 and C7h 5 parameters in ILI9806H V091; c06d75d0 sends 3 and 4. */
/* sharp_brain_pw_sh4 ddraw_mx28.dll PanelInitSeq c06d6d10 (FFh, DFh, B6h, F7h, BDh, BEh,
   FAh, EDh). */
constexpr SettingCommand kSettingCommands[] = {
    {0xBCu, 26u}, {0xC0u, 1u}, {0xB1u, 3u}, {0xB4u, 3u}, {0xC1u, 3u},
    {0xC6u, 2u},  {0xC7u, 4u}, {0xE0u, 16u}, {0xE1u, 16u},
    {0xFFu, 3u},  {0xDFu, 3u}, {0xB6u, 1u},  {0xF7u, 1u},  {0xBDu, 8u},
    {0xBEu, 17u}, {0xFAu, 5u}, {0xEDu, 4u},
};

struct FixedParamCommand {
    uint8_t cmd;
    uint8_t value;
};

/* MIPI DCS set_address_mode 36h, set_pixel_format 3Ah, set_tear_on 35h;
   sharp_brain_pw_sh4 ddraw_mx28.dll PanelInitSeq c06d6d10 (D0h, 55h, 00h). */
constexpr FixedParamCommand kFixedParamCommands[] = {
    {0x36u, 0xD0u}, {0x3Au, 0x55u}, {0x35u, 0x00u},
};

/* ILI9806H V091 datasheet pages 199-208 (10h, 11h, 13h, 22h, 28h, 29h); sharp_brain_pw_sh4
   ddraw_mx28.dll PanelInitSeq c06d6d10 (22h), PanelPowerOff c06d7424 (10h), LcdifUpdateRegion
   c06d7df0 (13h), PanelDisplayOnOff c06d865c (28h). */
constexpr uint8_t kSleepIn    = 0x10u;
constexpr uint8_t kSleepOut   = 0x11u;
constexpr uint8_t kNormalOn   = 0x13u;
constexpr uint8_t kAllPixOff  = 0x22u;
constexpr uint8_t kDisplayOff = 0x28u;
constexpr uint8_t kDisplayOn  = 0x29u;
constexpr uint8_t kNoParamCommands[] = {kSleepIn, kSleepOut, kNormalOn, kAllPixOff, kDisplayOff,
                                        kDisplayOn};

const FixedParamCommand* FindFixedParam(uint8_t cmd) {
    for (const auto& c : kFixedParamCommands)
        if (c.cmd == cmd) return &c;
    return nullptr;
}

bool IsNoParamCommand(uint8_t cmd) {
    for (uint8_t c : kNoParamCommands)
        if (c == cmd) return true;
    return false;
}

uint32_t SettingParamCount(uint8_t cmd) {
    for (const auto& c : kSettingCommands)
        if (c.cmd == cmd) return c.params;
    return 0u;
}

class SharpBrainPwSh4Panel : public MpuLcdPanel {
public:
    using MpuLcdPanel::MpuLcdPanel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    /* sharp_brain_pw_sh4 ddraw_mx28.dll LcdifObjInit c06d86bc (no panel init), PanelPowerOn
       c06d74a0 (not called at boot). */
    void OnReady() override {
        gram_.assign(static_cast<size_t>(kColumns) * kPages, 0u);
        blank_.assign(gram_.size(), 0u);
        emu_.Get<StateBootGate>().RegisterColdHandoff([this] { Handoff(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (!emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) Handoff();
        });
    }

    void WriteCommand(uint32_t bus_word) override;
    void WriteData(uint32_t bus_word) override;

    PanelSurface Gram() const override {
        const bool visible = sleep_ == 0u && display_on_ != 0u && all_pixels_off_ == 0u;
        return PanelSurface{.fb     = reinterpret_cast<const uint8_t*>(visible ? gram_.data()
                                                                                : blank_.data()),
                            .stride = kColumns * 2u,
                            .width  = kColumns,
                            .height = kPages};
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("gram", gram_.data(), gram_.size() * sizeof(gram_[0]));
        w.Write("window", window_);
        w.Write("cursor", cursor_);
        w.Write("command", command_);
        w.Write("params", params_);
        w.Write("param_count", param_count_);
        w.Write<uint8_t>("window_valid", window_valid_);
        w.Write<uint8_t>("sleep", sleep_);
        w.Write<uint8_t>("display_on", display_on_);
        w.Write<uint8_t>("all_pixels_off", all_pixels_off_);
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes("gram", gram_.data(), gram_.size() * sizeof(gram_[0]));
        r.Read("window", window_);
        r.Read("cursor", cursor_);
        r.Read("command", command_);
        r.Read("params", params_);
        r.Read("param_count", param_count_);
        r.Read("window_valid", window_valid_);
        r.Read("sleep", sleep_);
        r.Read("display_on", display_on_);
        r.Read("all_pixels_off", all_pixels_off_);
    }

private:
    [[noreturn]] void Die(const char* what, uint32_t value) const {
        emu_.Get<Fatal>().Die("PW-SH4 panel: %s (value %04X, command %02X)", what, value,
                              command_);
    }

    void EndAddressSet();

    void SetDisplayState(uint8_t cmd);
    void Handoff() {
        sleep_          = 0u;
        display_on_     = 1u;
        all_pixels_off_ = 0u;
    }

    std::vector<uint16_t> gram_;
    std::vector<uint16_t> blank_;
    uint32_t window_[4]   = {};
    uint32_t cursor_[2]   = {};
    uint8_t  params_[4]   = {};
    uint32_t param_count_ = 0u;
    uint8_t  command_     = 0u;
    uint8_t  window_valid_ = 0u;
    uint8_t  sleep_           = 1u;
    uint8_t  display_on_      = 0u;
    uint8_t  all_pixels_off_  = 0u;
};

/* ILI9806H V091 datasheet pages 199-208 default and blank states. */
void SharpBrainPwSh4Panel::SetDisplayState(uint8_t cmd) {
    switch (cmd) {
        case kSleepIn:    sleep_ = 1u; break;
        case kSleepOut:   sleep_ = 0u; break;
        case kNormalOn:   all_pixels_off_ = 0u; break;
        case kAllPixOff:  all_pixels_off_ = 1u; break;
        case kDisplayOff: display_on_ = 0u; break;
        case kDisplayOn:  display_on_ = 1u; break;
        default:          break;
    }
}

/* ILI9341 V1.11 section 8.2.22: any other command stops a frame write. */
void SharpBrainPwSh4Panel::WriteCommand(uint32_t bus_word) {
    const uint8_t cmd = static_cast<uint8_t>(bus_word & 0xFFu);
    command_     = cmd;
    param_count_ = 0u;
    if (IsNoParamCommand(cmd)) {
        SetDisplayState(cmd);
        return;
    }
    if (SettingParamCount(cmd) != 0u || FindFixedParam(cmd)) return;
    if (cmd != kCaset && cmd != kPaset && cmd != kRamwr) return;
    if (cmd != kRamwr) return;
    if (window_valid_ != 0x3u) {
        window_[0] = 0;
        window_[1] = kColumns - 1;
        window_[2] = 0;
        window_[3] = kPages - 1;
        window_valid_ = 0x3u;
    }
    cursor_[0] = window_[0];
    cursor_[1] = window_[2];
}

/* ILI9341 V1.11 sections 8.2.20, 8.2.21: parameters carry D7-D0, high byte first. */
void SharpBrainPwSh4Panel::EndAddressSet() {
    const uint32_t start = (static_cast<uint32_t>(params_[0]) << 8) | params_[1];
    const uint32_t end   = (static_cast<uint32_t>(params_[2]) << 8) | params_[3];
    const uint32_t limit = command_ == kCaset ? kColumns : kPages;
    if (start > end || end >= limit) return;
    const uint32_t base = command_ == kCaset ? 0u : 2u;
    window_[base]      = start;
    window_[base + 1u] = end;
    window_valid_ |= command_ == kCaset ? 0x1u : 0x2u;
}

/* ILI9341 V1.11 section 8.2.22: data goes to the frame memory and the column then page
   counters advance inside the window. */
void SharpBrainPwSh4Panel::WriteData(uint32_t bus_word) {
    if (command_ == kCaset || command_ == kPaset) {
        if (param_count_ >= 4u) return;
        params_[param_count_++] = static_cast<uint8_t>(bus_word & 0xFFu);
        if (param_count_ == 4u) EndAddressSet();
        return;
    }
    const uint32_t setting_params = SettingParamCount(command_);
    if (setting_params != 0u) {
        if (param_count_ < setting_params) ++param_count_;
        return;
    }
    if (FindFixedParam(command_)) {
        if (param_count_ == 0u) ++param_count_;
        return;
    }
    if (command_ != kRamwr) {
        ++param_count_;
        return;
    }
    if (cursor_[1] > window_[3]) return;
    gram_[static_cast<size_t>(cursor_[1]) * kColumns + cursor_[0]] =
        static_cast<uint16_t>(bus_word & 0xFFFFu);
    if (cursor_[0] < window_[1]) {
        ++cursor_[0];
        return;
    }
    cursor_[0] = window_[0];
    ++cursor_[1];
}

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4Panel, MpuLcdPanel);

}
