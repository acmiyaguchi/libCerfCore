#include "../../socs/sa11xx/sa11xx_ssp_device.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../socs/sa11xx/sa11xx_ppc.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include "hp_jornada_700_battery.h"
#include "hp_jornada_700_keyboard.h"
#include "hp_jornada_700_led.h"
#include "hp_jornada_700_touch.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

namespace {

/* HP doc §4.2: both directions are bit-reversed within each byte ("to send
   0xCA, shuffle to 0x53"); the OAL's reverse helper is nk.exe sub_8004F2BC. */
uint8_t BitRev8(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    return (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
}

/* Jornada 720 keyboard/touch/battery/PWM MCU: HP doc §4; jornada720 nk.exe sub_8004F244. */
class HpJornada700Mcu : public Sa11xxSspDevice {
public:
    using Sa11xxSspDevice::Sa11xxSspDevice;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    void OnReady() override {
        ppc_ = &emu_.Get<Sa11xxPpc>();
        ppc_->RegisterOutputListener([this] { OnPpcOutputs(); });
    }

    Pins WiredTo() const override { return Pins::SerialPort4; }

    uint16_t Exchange(uint16_t tx_frame) override {
        const uint8_t byte = BitRev8(static_cast<uint8_t>(tx_frame));
        return BitRev8(Consume(byte));
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint8_t>("mcu_contrast", contrast_);
        w.Write<uint8_t>("mcu_brightness", brightness_);
        w.Write<uint32_t>("mcu_param_bytes_left", param_bytes_left_);
        w.Write<uint8_t>("mcu_param", static_cast<uint8_t>(param_));
        w.Write<uint32_t>("mcu_response_count", static_cast<uint32_t>(response_.size()));
        for (uint8_t b : response_) w.Write<uint8_t>("mcu_response_byte", b);
        emu_.Get<HpJornada700Touch>().SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        uint8_t  param = 0;
        uint32_t count = 0;
        r.Read("mcu_contrast", contrast_);
        r.Read("mcu_brightness", brightness_);
        r.Read("mcu_param_bytes_left", param_bytes_left_);
        r.Read("mcu_param", param);
        r.Read("mcu_response_count", count);
        param_ = static_cast<Param>(param);
        response_.clear();
        for (uint32_t i = 0; i < count; ++i) {
            uint8_t b = 0;
            r.Read("mcu_response_byte", b);
            response_.push_back(b);
        }
        emu_.Get<HpJornada700Touch>().RestoreState(r);
    }

    void PostRestore() override {
        lfclk_low_ = LfclkDrivenLow();
        emu_.Get<HpJornada700Touch>().PostRestore();
    }

private:
    enum class Param : uint8_t { None, Contrast, Brightness, TouchRate };

    /* HP Jornada 720 Hardware Configuration § 4.1; SA-1110 Dev Man PPDR bit 10
       L_FCLK (printed 11-169); jornada720 nk.exe sub_8004F824. */
    static constexpr uint32_t kLfclkPin = 1u << 10;
    static constexpr uint8_t kPowerOnContrast   = 0x36;
    static constexpr uint8_t kPowerOnBrightness = 0x19;

    bool LfclkDrivenLow() const { return (ppc_->LcdPinsDrivenLow() & kLfclkPin) != 0u; }

    void OnPpcOutputs() {
        const bool low = LfclkDrivenLow();
        if (low && !lfclk_low_) ResetMcu();
        lfclk_low_ = low;
    }

    void ResetMcu() {
        LOG(Periph, "[J720MCU] reset: L_FCLK driven low\n");
        response_.clear();
        param_bytes_left_ = 0;
        param_            = Param::None;
        contrast_         = kPowerOnContrast;
        brightness_       = kPowerOnBrightness;
        std::vector<uint8_t> discarded;
        emu_.Get<HpJornada700Keyboard>().DrainScancodes(discarded);
        emu_.Get<HpJornada700Led>().SetState(HpJornada700Led::State::Off);
        emu_.Get<HpJornada700Touch>().ResetSampleRate();
    }

    /* HP doc §4.7 command values (+ the 0xD5/0xD7 setters this ROM's OAL
       actually uses - sub_8004F454 sends D5/D7, level, 0x3F trailer). */
    enum : uint8_t {
        kTxDummy        = 0x11,
        /* The OAL sends the read-clock dummy as raw wire 0x11 (not bit-reversed
           like commands), so it arrives here as BitRev8(0x11)=0x88. */
        kTxDummyRaw     = 0x88,
        kGetScanKeyCode = 0x90,
        kKbdInitParam   = 0x95,
        kGetTouchSamples= 0xA0,
        /* jornada720 touch.dll sub_FB1750, spidrv.dll sub_FE146C. */
        kSetTouchRate   = 0xA2,
        kGetBatteryData = 0xC0,
        kGetContrast    = 0xD0,
        kSetContrast    = 0xD1,
        kGetBrightness  = 0xD2,
        kSetBrightness  = 0xD3,
        kSetContrast2   = 0xD5,
        kSetBrightness2 = 0xD7,
        kContrastOff    = 0xD8,
        kBrightnessOff  = 0xD9,
        kPwmOff         = 0xDF,
        /* Notification LED (gwes.exe nled driver sub_926CC / sub_927C4). */
        kGetLedStatus   = 0xE0,
        kLedOn          = 0xE1,
        kLedBlink       = 0xE2,
        kLedOff         = 0xE3,
    };

    /* battdrv.dll sub_ED1814 maps main-battery ADC to percent in 6 bands using
       the table @0xED1220 = {512,585,628,638,651,665}: <512 -> 0%, >=665 ->
       100%, else 100*band/6. Pick the upper half of each band so the driver's
       downward-hysteresis midpoint reduction (sub_ED1814) can't drop a bucket. */
    static uint16_t MainAdcForFill(int fill) {
        static const uint16_t kRep[7] = { 500, 560, 615, 635, 647, 660, 700 };
        if (fill < 0)   fill = 0;
        if (fill > 100) fill = 100;
        return kRep[(fill * 6 + 50) / 100];   /* band = round(fill*6/100) */
    }

    uint8_t Consume(uint8_t byte) {
        if (param_bytes_left_ > 0) {
            --param_bytes_left_;
            TakeParam(byte);
            return kTxDummy;
        }
        if (!response_.empty()) {
            const uint8_t r = response_.front();
            response_.pop_front();
            return r;
        }
        if (byte == kTxDummy || byte == kTxDummyRaw) return kTxDummy;
        return Command(byte);
    }

    void TakeParam(uint8_t byte) {
        switch (param_) {
            case Param::Contrast:   contrast_ = byte; break;
            case Param::Brightness: brightness_ = byte; break;
            case Param::TouchRate:  emu_.Get<HpJornada700Touch>().SelectSampleRate(byte); break;
            case Param::None:       break;
        }
        param_ = Param::None;
    }

    uint8_t Command(uint8_t cmd) {
        response_.clear();
        switch (cmd) {
            case kGetScanKeyCode: {
                /* §4.3: first TxDummy returns the scancode count, then the
                   codes; HpJornada700Keyboard owns the GPIO0-fed queue. */
                std::vector<uint8_t> codes;
                emu_.Get<HpJornada700Keyboard>().DrainScancodes(codes);
                response_.push_back((uint8_t)codes.size());
                for (uint8_t c : codes) response_.push_back(c);
                break;
            }
            case kGetTouchSamples: {
                /* touch.dll sub_FB1DD0 reads the 8 bytes as X1,X2,X3,Y1,Y2,Y3,
                   then a combined-X byte (s1/s2/s3 bits 8,9 at bit pairs 0/2/4)
                   and a combined-Y byte. Three identical samples per axis - the
                   driver's median filter (sub_FB1C38) collapses them. */
                uint16_t ax = 0, ay = 0;
                emu_.Get<HpJornada700Touch>().CurrentAdc(&ax, &ay);
                const uint8_t xl = (uint8_t)(ax & 0xFF);
                const uint8_t yl = (uint8_t)(ay & 0xFF);
                const uint8_t xc = (uint8_t)((ax >> 8) & 3);
                const uint8_t yc = (uint8_t)((ay >> 8) & 3);
                response_.push_back(xl);
                response_.push_back(xl);
                response_.push_back(xl);
                response_.push_back(yl);
                response_.push_back(yl);
                response_.push_back(yl);
                response_.push_back((uint8_t)(xc | (xc << 2) | (xc << 4)));
                response_.push_back((uint8_t)(yc | (yc << 2) | (yc << 4)));
                break;
            }
            case kGetBatteryData: {
                /* §4.5: 3 bytes [main_low, backup_low, combined], each ADC
                   10-bit; combined bits 0,1 = main 8,9 and bits 2,3 = backup 8,9.
                   Main mapped from the widget fill via the battery driver's own
                   bands; backup = a healthy coin cell. */
                const uint16_t main_adc =
                    MainAdcForFill(emu_.Get<HpJornada700Battery>().FillPercent());
                constexpr uint16_t kBackupAdc = 0x390;   /* >0x383 -> HIGH (battdrv.dll sub_ED1AC8). */
                response_.push_back((uint8_t)(main_adc & 0xFF));
                response_.push_back((uint8_t)(kBackupAdc & 0xFF));
                response_.push_back((uint8_t)(((main_adc >> 8) & 3) |
                                              (((kBackupAdc >> 8) & 3) << 2)));
                break;
            }
            case kGetContrast:   response_.push_back(contrast_);   break;
            case kGetBrightness: response_.push_back(brightness_); break;
            case kSetContrast:
                param_bytes_left_ = 1;
                param_ = Param::Contrast;
                break;
            case kSetBrightness:
                param_bytes_left_ = 1;
                param_ = Param::Brightness;
                break;
            case kSetTouchRate:
                param_bytes_left_ = 1;
                param_ = Param::TouchRate;
                break;
            case kKbdInitParam:
                /* Undocumented; TSCkbdr.dll sub_FC4248 sends 0x95 + 2 data
                   bytes at kbd-driver init. Consume them so they aren't
                   mis-parsed as commands; no observable readback. */
                param_bytes_left_ = 2;
                break;
            case kSetContrast2:               /* level + 0x3F trailer. */
                param_bytes_left_ = 2;
                param_ = Param::Contrast;
                break;
            case kSetBrightness2:
                param_bytes_left_ = 2;
                param_ = Param::Brightness;
                break;
            case kGetLedStatus:
                response_.push_back(emu_.Get<HpJornada700Led>().StatusByte());
                break;
            case kLedOn:
                emu_.Get<HpJornada700Led>().SetState(HpJornada700Led::State::On);
                break;
            case kLedBlink:
                emu_.Get<HpJornada700Led>().SetState(HpJornada700Led::State::Blink);
                break;
            case kLedOff:
                emu_.Get<HpJornada700Led>().SetState(HpJornada700Led::State::Off);
                break;
            case kContrastOff:
            case kBrightnessOff:
            case kPwmOff:
                break;                        /* PWM gating: no CERF effect. */
            default:
                LOG(Caution, "HpJornada700Mcu: unknown command 0x%02X - "
                    "answering TxDummy\n", cmd);
                break;
        }
#if CERF_DEV_MODE
        LOG(Periph, "[J720MCU] cmd 0x%02X (queued %zu response bytes, "
            "%u params expected)\n", cmd, response_.size(),
            param_bytes_left_);
#endif
        return kTxDummy;
    }

    std::deque<uint8_t> response_;
    uint32_t param_bytes_left_ = 0;
    Param    param_            = Param::None;
    uint8_t  contrast_         = kPowerOnContrast;
    uint8_t  brightness_       = kPowerOnBrightness;
    Sa11xxPpc* ppc_            = nullptr;
    bool       lfclk_low_      = false;
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada700Mcu, Sa11xxSspDevice);

}
