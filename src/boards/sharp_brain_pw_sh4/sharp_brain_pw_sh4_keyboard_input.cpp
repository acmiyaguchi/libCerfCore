#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "../../socs/imx28/imx28_pinctrl.h"
#include "../../socs/imx28/imx28_power.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kMatrixCode = 0x100u;
constexpr uint32_t kRows       = 7u;
constexpr uint32_t kColBank    = 4u;

/* sharp_brain_pw_sh4 keybd_EDNA2.dll KBD_ScanMatrix c0878678: DIN4 pins 0-4, 6, 7. */
constexpr uint32_t kColPins    = 0xDFu;

struct RowPin {
    uint32_t bank;
    uint32_t pin;
};

/* sharp_brain_pw_sh4 keybd_EDNA2.dll KBD_DriveRows c0878598, row masks c0872d3c. */
constexpr RowPin kRowPins[kRows] = {
    {2u, 16u}, {2u, 17u}, {2u, 18u}, {2u, 19u}, {2u, 20u}, {2u, 21u}, {4u, 8u},
};

class SharpBrainPwSh4KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        pins_ = &emu_.Get<Imx28Pinctrl>();
        pins_->AddPinLevelProvider(
            [this](uint32_t bank, uint32_t pin, bool* high) { return ColumnLevel(bank, pin, high); });
        emu_.Get<KeyboardRouter>().Register(this);
    }

    std::string SourceName() const override { return "PW-SH4 keyboard"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        if (code < kMatrixCode) {
            emu_.Get<Imx28Power>().SetPswitchLevel(key_up ? 0u : code);
            return;
        }
        const uint32_t cell = code - kMatrixCode;
        const uint64_t bit = 1ull << cell;
        if (key_up) pressed_.fetch_and(~bit);
        else        pressed_.fetch_or(bit);
        pins_->NotifyPinLevelChanged(kColBank, cell % 8u);
    }

private:
    bool ColumnLevel(uint32_t bank, uint32_t pin, bool* high) const {
        if (bank != kColBank || pin >= 8u || ((kColPins >> pin) & 1u) == 0u) return false;
        const uint64_t pressed = pressed_.load();
        bool low = false;
        for (uint32_t r = 0; r < kRows && !low; ++r) {
            if (((pressed >> (r * 8u + pin)) & 1u) == 0u) continue;
            low = pins_->DrivesLow(kRowPins[r].bank, kRowPins[r].pin);
        }
        *high = !low;
        return true;
    }

    Imx28Pinctrl* pins_ = nullptr;
    std::atomic<uint64_t> pressed_{0u};
};

}  /* namespace */

REGISTER_SERVICE(SharpBrainPwSh4KeyboardInput);

}
