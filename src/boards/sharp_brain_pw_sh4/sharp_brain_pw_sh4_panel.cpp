#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
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

class SharpBrainPwSh4Panel : public MpuLcdPanel {
public:
    using MpuLcdPanel::MpuLcdPanel;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override { gram_.assign(static_cast<size_t>(kColumns) * kPages, 0u); }

    void WriteCommand(uint32_t bus_word) override;
    void WriteData(uint32_t bus_word) override;

    PanelSurface Gram() const override {
        return PanelSurface{.fb     = reinterpret_cast<const uint8_t*>(gram_.data()),
                            .stride = kColumns * 2u,
                            .width  = kColumns,
                            .height = kPages};
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("gram", gram_.data(), gram_.size());
        w.Write("window", window_);
        w.Write("cursor", cursor_);
        w.Write("command", command_);
        w.Write("params", params_);
        w.Write("param_count", param_count_);
        w.Write<uint8_t>("window_valid", window_valid_);
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes("gram", gram_.data(), gram_.size());
        r.Read("window", window_);
        r.Read("cursor", cursor_);
        r.Read("command", command_);
        r.Read("params", params_);
        r.Read("param_count", param_count_);
        r.Read("window_valid", window_valid_);
    }

private:
    [[noreturn]] void Die(const char* what, uint32_t value) const {
        emu_.Get<Fatal>().Die("PW-SH4 panel: %s (value %04X, command %02X)", what, value,
                              command_);
    }

    void EndAddressSet();

    std::vector<uint16_t> gram_;
    uint32_t window_[4]   = {};
    uint32_t cursor_[2]   = {};
    uint8_t  params_[4]   = {};
    uint32_t param_count_ = 0u;
    uint8_t  command_     = 0u;
    uint8_t  window_valid_ = 0u;
};

/* ILI9341 V1.11 section 8.2.22: any other command stops a frame write. */
void SharpBrainPwSh4Panel::WriteCommand(uint32_t bus_word) {
    const uint8_t cmd = static_cast<uint8_t>(bus_word & 0xFFu);
    if (cmd != kCaset && cmd != kPaset && cmd != kRamwr) {
        command_ = cmd;
        Die("unmodelled command", bus_word);
    }
    command_     = cmd;
    param_count_ = 0u;
    if (cmd != kRamwr) return;
    if (window_valid_ != 0x3u) Die("RAMWR before CASET and PASET", bus_word);
    cursor_[0] = window_[0];
    cursor_[1] = window_[2];
}

/* ILI9341 V1.11 sections 8.2.20, 8.2.21: parameters carry D7-D0, high byte first. */
void SharpBrainPwSh4Panel::EndAddressSet() {
    const uint32_t start = (static_cast<uint32_t>(params_[0]) << 8) | params_[1];
    const uint32_t end   = (static_cast<uint32_t>(params_[2]) << 8) | params_[3];
    const uint32_t limit = command_ == kCaset ? kColumns : kPages;
    if (start > end || end >= limit) Die("address set out of range", (start << 16) | end);
    const uint32_t base = command_ == kCaset ? 0u : 2u;
    window_[base]      = start;
    window_[base + 1u] = end;
    window_valid_ |= command_ == kCaset ? 0x1u : 0x2u;
}

/* ILI9341 V1.11 section 8.2.22: data goes to the frame memory and the column then page
   counters advance inside the window. */
void SharpBrainPwSh4Panel::WriteData(uint32_t bus_word) {
    if (command_ == kCaset || command_ == kPaset) {
        if (param_count_ >= 4u) Die("extra address parameter", bus_word);
        params_[param_count_++] = static_cast<uint8_t>(bus_word & 0xFFu);
        if (param_count_ == 4u) EndAddressSet();
        return;
    }
    if (command_ != kRamwr) Die("data with no open command", bus_word);
    if (cursor_[1] > window_[3]) Die("RAMWR past the window end", bus_word);
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
