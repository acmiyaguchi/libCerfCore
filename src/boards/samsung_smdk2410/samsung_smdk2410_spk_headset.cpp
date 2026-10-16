#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <cstdint>
#include <mutex>

namespace {

constexpr uint32_t kBase = 0x500FFFC0u;
constexpr uint32_t kSize = 0x00000001u;

constexpr uint32_t kRegSpeakerPhoneState = 0x00u;

class SamsungSmdk2410SpkHeadset : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte (uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        w.Write("speaker_phone_state", speaker_phone_state_);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(state_mutex_);
        r.Read("speaker_phone_state", speaker_phone_state_);
    }

private:
    mutable std::mutex state_mutex_;
    uint8_t            speaker_phone_state_ = 0;
};

uint8_t SamsungSmdk2410SpkHeadset::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    uint8_t value = 0;
    {
        std::lock_guard<std::mutex> lk(state_mutex_);
        switch (off) {
            case kRegSpeakerPhoneState:
                value = speaker_phone_state_;
                break;
            default:
                HaltUnsupportedAccess("ReadByte", addr, 0);  /* noreturn */
        }
    }
    LOG(Periph, "[SpkHeadset] read8 SpeakerPhoneState -> 0x%02X\n", value);
    return value;
}

void SamsungSmdk2410SpkHeadset::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    LOG(Periph, "[SpkHeadset] write8 SpeakerPhoneState = 0x%02X\n", value);
    std::lock_guard<std::mutex> lk(state_mutex_);
    switch (off) {
        case kRegSpeakerPhoneState:
            speaker_phone_state_ = value;
            break;
        default:
            HaltUnsupportedAccess("WriteByte", addr, value);  /* noreturn */
    }
}

}  /* namespace */

REGISTER_SERVICE(SamsungSmdk2410SpkHeadset);
